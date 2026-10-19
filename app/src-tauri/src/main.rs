//! Samplooler Library: the instruments for the Stack, installed from a computer. The window (ui/) lists
//! the library and the boards; this side finds the boards, downloads and keeps firmwares, flashes them
//! and keeps a MIDI line open to a board for its settings and log.

#![cfg_attr(not(debug_assertions), windows_subsystem = "windows")]

mod boards;
mod catalog;
mod flash;
mod local;
mod midi;

use catalog::{Catalog, Source};
use serde::{Deserialize, Serialize};
use serde_json::{json, Value};
use std::collections::HashMap;
use std::path::PathBuf;
use std::sync::atomic::{AtomicBool, Ordering};
use std::sync::{Arc, Mutex, RwLock};
use std::time::{Duration, Instant};
use tauri::{AppHandle, Emitter, Manager};

#[derive(Serialize, Deserialize, Default, Clone)]
struct Installed {
    id: String,
    name: String,
    version: String,
    at: u64,
}

#[derive(Serialize, Deserialize, Default, Clone)]
struct Config {
    /// the library's address or folder; None: the project page
    source: Option<String>,
    /// what this app last put on each board, by the MAC's last four bytes
    installed: HashMap<String, Installed>,
}

#[derive(Default)]
struct App {
    catalog: RwLock<Option<Arc<Catalog>>>,
    seen: boards::Seen,
    busy: AtomicBool,
    line: Mutex<Option<midi::Line>>,
    config: Mutex<Config>,
}

fn config_path(app: &AppHandle) -> PathBuf {
    app.path().app_config_dir().unwrap_or_else(|_| PathBuf::from(".")).join("config.json")
}

fn cache_dir(app: &AppHandle) -> PathBuf {
    app.path().app_cache_dir().unwrap_or_else(|_| std::env::temp_dir()).join("library")
}

fn save_config(app: &AppHandle) {
    let c = app.state::<App>().config.lock().unwrap().clone();
    let p = config_path(app);
    if let Some(d) = p.parent() {
        let _ = std::fs::create_dir_all(d);
    }
    let _ = std::fs::write(p, serde_json::to_vec_pretty(&c).unwrap_or_default());
}

fn source(app: &AppHandle) -> Source {
    if let Ok(s) = std::env::var("SAMPLOOLER_LIBRARY") {
        return Source::parse(&s);
    }
    let c = app.state::<App>().config.lock().unwrap().source.clone();
    Source::parse(c.as_deref().unwrap_or(catalog::DEFAULT_SOURCE))
}

fn now() -> u64 {
    std::time::SystemTime::now().duration_since(std::time::UNIX_EPOCH).map(|d| d.as_secs()).unwrap_or(0)
}

/// Run blocking work (the web, MIDI, the serial port) off the window's thread.
async fn blocking<T: Send + 'static>(
    app: AppHandle,
    f: impl FnOnce(&AppHandle) -> Result<T, String> + Send + 'static,
) -> Result<T, String> {
    tauri::async_runtime::spawn_blocking(move || f(&app)).await.map_err(|e| e.to_string())?
}

fn current(app: &AppHandle) -> Result<Arc<Catalog>, String> {
    app.state::<App>().catalog.read().unwrap().clone().ok_or_else(|| "the library is not loaded".into())
}

fn names(app: &AppHandle) -> Vec<String> {
    current(app)
        .map(|c| c.entries().iter().filter_map(|e| e["name"].as_str().map(|s| s.to_string())).collect())
        .unwrap_or_default()
}

// ---------------------------------------------------------------- the library

#[tauri::command]
async fn library(app: AppHandle, reload: bool) -> Result<Value, String> {
    blocking(app, move |app| {
        let st = app.state::<App>();
        let have = st.catalog.read().unwrap().clone();
        let cat = match have {
            Some(c) if !reload => c,
            _ => {
                let c = Arc::new(Catalog::load(source(app), cache_dir(app))?);
                *st.catalog.write().unwrap() = Some(c.clone());
                c
            }
        };
        let entries: Vec<Value> = cat
            .entries()
            .into_iter()
            .map(|mut e| {
                let id = e["id"].as_str().unwrap_or("").to_string();
                e["kept"] = Value::Bool(cat.has_firmware(&id));
                e
            })
            .collect();
        Ok(json!({
            "source": cat.source.describe(),
            "web": matches!(cat.source, Source::Web(_)),
            "offline": cat.offline,
            "updated": cat.data["updated"],
            "repo": cat.data["repo"],
            "entries": entries,
        }))
    })
    .await
}

#[tauri::command]
async fn set_source(app: AppHandle, source: Option<String>) -> Result<(), String> {
    {
        let st = app.state::<App>();
        st.config.lock().unwrap().source = source.filter(|s| !s.trim().is_empty());
        *st.catalog.write().unwrap() = None;
    }
    save_config(&app);
    Ok(())
}

#[tauri::command]
fn settings(app: AppHandle) -> Value {
    let st = app.state::<App>();
    let c = st.config.lock().unwrap();
    json!({
        "source": c.source,
        "default_source": catalog::DEFAULT_SOURCE,
        "env_source": std::env::var("SAMPLOOLER_LIBRARY").ok(),
        "cache": cache_dir(&app).display().to_string(),
        "version": env!("CARGO_PKG_VERSION"),
    })
}

#[tauri::command]
async fn clear_cache(app: AppHandle) -> Result<(), String> {
    blocking(app, |app| {
        let d = cache_dir(app);
        if d.is_dir() {
            std::fs::remove_dir_all(&d).map_err(|e| e.to_string())?;
        }
        Ok(())
    })
    .await
}

// ---------------------------------------------------------------- boards

fn installed_map(app: &AppHandle) -> HashMap<String, (String, String)> {
    app.state::<App>()
        .config
        .lock()
        .unwrap()
        .installed
        .iter()
        .map(|(k, v)| (k.clone(), (v.name.clone(), v.version.clone())))
        .collect()
}

#[tauri::command]
async fn boards(app: AppHandle) -> Result<Vec<boards::Board>, String> {
    blocking(app, |app| {
        let st = app.state::<App>();
        if st.busy.load(Ordering::SeqCst) {
            return Err("busy".into());
        }
        Ok(boards::list(&names(app), &st.seen, &installed_map(app)))
    })
    .await
}

// ---------------------------------------------------------------- installing

#[derive(Serialize, Clone, Default)]
struct Job {
    /// download, reboot, connect, write, verify, restart, back, done, error
    stage: &'static str,
    text: String,
    done: usize,
    total: usize,
    part: usize,
    parts: usize,
}

fn emit(app: &AppHandle, j: Job) {
    let _ = app.emit("job", j);
}

fn stage(app: &AppHandle, stage: &'static str, text: impl Into<String>) {
    emit(app, Job { stage, text: text.into(), ..Default::default() });
}

/// Ask a running board to reboot into download mode and wait for its serial port.
fn reboot_to_download(port: &str, tail: Option<&str>) -> Result<String, String> {
    let before: Vec<String> = boards::download_ports().into_iter().map(|p| p.0).collect();
    midi::send(port, &midi::REBOOT)?;
    let t0 = Instant::now();
    while t0.elapsed() < Duration::from_secs(12) {
        std::thread::sleep(Duration::from_millis(200));
        let now = boards::download_ports();
        if let Some(t) = tail {
            if let Some(p) = now.iter().find(|(_, mac)| boards::mac_tail(mac).as_deref() == Some(t)) {
                return Ok(p.0.clone());
            }
        } else {
            let new: Vec<&(String, String)> = now.iter().filter(|p| !before.contains(&p.0)).collect();
            if new.len() == 1 {
                return Ok(new[0].0.clone());
            }
        }
    }
    Err("the board did not come back in download mode. Unplug it, hold BOOT while you plug it in, and pick it from the list".into())
}

/// After the restart, wait for the board to show up running. Returns the name it runs.
fn wait_running(app: &AppHandle, tail: &str) -> Option<String> {
    let st = app.state::<App>();
    boards::forget(&st.seen);
    let t0 = Instant::now();
    while t0.elapsed() < Duration::from_secs(15) {
        std::thread::sleep(Duration::from_millis(500));
        let found = boards::list(&names(app), &st.seen, &HashMap::new());
        if let Some(b) = found.iter().find(|b| b.mode == "running" && b.mac_tail.as_deref() == Some(tail)) {
            return b.name.clone();
        }
        // a firmware that cannot say its serial: the only running board that appeared
        let running: Vec<_> = found.iter().filter(|b| b.mode == "running").collect();
        if running.len() == 1 && running[0].mac_tail.is_none() {
            return running[0].name.clone();
        }
    }
    None
}

fn run_install(app: &AppHandle, board: &str, id: Option<String>, build: Option<String>) -> Result<(), String> {
    // 1. the firmware
    let (parts, name, version, iid) = if let Some(dir) = build {
        let b = local::inspect(&dir)?;
        stage(app, "download", format!("Reading the build in {}", b.dir));
        (local::read(&b)?, b.name.to_uppercase(), b.version.clone(), b.name.clone())
    } else {
        let id = id.ok_or("nothing to install")?;
        let cat = current(app)?;
        let e = cat.entry(&id).ok_or_else(|| format!("{id} is not in the library"))?;
        let name = e["name"].as_str().unwrap_or(&id).to_string();
        let version = e["version"].as_str().unwrap_or("").to_string();
        stage(app, "download", format!("Getting {name} {version}"));
        let parts = cat.firmware(&id, |done, total| {
            emit(app, Job { stage: "download", text: format!("Getting {name} {version}"), done, total, ..Default::default() })
        })?;
        (parts, name, version, id)
    };
    let nparts = parts.len();

    // 2. the board, in download mode
    let st = app.state::<App>();
    let (port, tail) = if let Some(midi_port) = board.strip_prefix("midi:") {
        let found = boards::list(&names(app), &st.seen, &HashMap::new());
        let b = found.iter().find(|b| b.port == midi_port).ok_or("the board is not there any more")?;
        // the settings line holds the port open; it goes, since the board is about to restart
        if st.line.lock().unwrap().as_ref().map_or(false, |l| l.port == midi_port) {
            *st.line.lock().unwrap() = None;
        }
        stage(app, "reboot", format!("Asking {} to restart into download mode", b.name.clone().unwrap_or_default()));
        let tail = b.mac_tail.clone();
        (reboot_to_download(midi_port, tail.as_deref())?, tail)
    } else if let Some(p) = board.strip_prefix("dl:") {
        // a board already in download mode has no name to go by: it is written only when it is the only one
        // (tools/flash.sh's rule), and only if it still answers on that port
        let dl = boards::download_ports();
        if dl.len() > 1 {
            return Err(format!("{} boards are in download mode: unplug the others, or start them so they show up by name", dl.len()));
        }
        let mac = dl.into_iter().find(|x| x.0 == p).ok_or("the board is not there any more")?.1;
        (p.to_string(), boards::mac_tail(&mac))
    } else {
        return Err("no board picked".into());
    };

    // 3. writing
    stage(app, "connect", format!("Connecting on {port}"));
    let names_of: Vec<String> = parts.iter().map(|p| p.name.clone()).collect();
    let mac = flash::write(&port, &parts, tail.as_deref(), |s| match s {
        flash::Step::Connected { mac } => stage(app, "connect", format!("Connected to {mac}")),
        flash::Step::Writing { part, done, total } => emit(app, Job {
            stage: "write",
            text: format!("Writing {}", names_of.get(part).cloned().unwrap_or_default()),
            done,
            total,
            part,
            parts: nparts,
        }),
        flash::Step::Verifying { part } => emit(app, Job {
            stage: "verify",
            text: format!("Checking {}", names_of.get(part).cloned().unwrap_or_default()),
            part,
            parts: nparts,
            ..Default::default()
        }),
        flash::Step::Restarting => stage(app, "restart", "Restarting the board"),
    })?;
    let tail = boards::mac_tail(&mac).unwrap_or_default();
    {
        let mut c = st.config.lock().unwrap();
        c.installed.insert(tail.clone(), Installed { id: iid, name: name.clone(), version: version.clone(), at: now() });
    }
    save_config(app);

    // 4. back up and running
    stage(app, "back", format!("Waiting for {name} to start"));
    match wait_running(app, &tail) {
        Some(n) => stage(app, "done", format!("{n} is on the board ({mac})")),
        None => stage(app, "done", format!("{name} is written ({mac}). It has not shown up on USB yet: unplug the board and plug it in again")),
    }
    Ok(())
}

#[tauri::command]
fn install(app: AppHandle, board: String, id: Option<String>, build: Option<String>) -> Result<(), String> {
    let st = app.state::<App>();
    if st.busy.swap(true, Ordering::SeqCst) {
        return Err("already installing".into());
    }
    let a = app.clone();
    std::thread::spawn(move || {
        if let Err(e) = run_install(&a, &board, id, build) {
            stage(&a, "error", e);
        }
        a.state::<App>().busy.store(false, Ordering::SeqCst);
        boards::forget(&a.state::<App>().seen);
    });
    Ok(())
}

#[tauri::command]
async fn inspect_build(app: AppHandle, dir: String) -> Result<local::Build, String> {
    blocking(app, move |_| local::inspect(&dir)).await
}

// ---------------------------------------------------------------- a line to one board

#[tauri::command]
async fn line_open(app: AppHandle, port: String) -> Result<(), String> {
    blocking(app, move |app| {
        let st = app.state::<App>();
        *st.line.lock().unwrap() = None;
        let a = app.clone();
        let p = port.clone();
        let mut line = midi::Line::open(&port, move |m| {
            let _ = a.emit("board", json!({ "port": p, "msg": m }));
        })?;
        line.send(&midi::DESCRIBE)?;
        *st.line.lock().unwrap() = Some(line);
        Ok(())
    })
    .await
}

#[tauri::command]
fn line_close(app: AppHandle) {
    *app.state::<App>().line.lock().unwrap() = None;
}

fn with_line(app: &AppHandle, f: impl FnOnce(&mut midi::Line) -> Result<(), String>) -> Result<(), String> {
    let st = app.state::<App>();
    let mut g = st.line.lock().unwrap();
    let l = g.as_mut().ok_or("no board is open")?;
    f(l)
}

#[tauri::command]
fn line_set(app: AppHandle, tag: String, value: String) -> Result<(), String> {
    with_line(&app, |l| l.set(&tag, &value))
}

#[tauri::command]
fn line_cc(app: AppHandle, channel: u8, cc: u8, value: u8) -> Result<(), String> {
    with_line(&app, |l| l.cc(channel, cc, value))
}

#[tauri::command]
fn line_describe(app: AppHandle) -> Result<(), String> {
    with_line(&app, |l| l.send(&midi::DESCRIBE))
}

#[tauri::command]
fn line_status(app: AppHandle) -> Result<(), String> {
    with_line(&app, |l| l.send(&midi::STATUS))
}

/// The window says it is still there (every 2 s): a board stops sending changes after 5 s of silence.
#[tauri::command]
fn line_ping(app: AppHandle) -> Result<(), String> {
    with_line(&app, |l| l.send(&midi::IDENTIFY))
}

// ---------------------------------------------------------------- lib:// (the library's files)

fn unescape(s: &str) -> String {
    let b = s.as_bytes();
    let mut out = Vec::with_capacity(b.len());
    let mut i = 0;
    while i < b.len() {
        if b[i] == b'%' && i + 2 < b.len() {
            if let Ok(v) = u8::from_str_radix(&s[i + 1..i + 3], 16) {
                out.push(v);
                i += 3;
                continue;
            }
        }
        out.push(b[i]);
        i += 1;
    }
    String::from_utf8_lossy(&out).into_owned()
}

fn mime(path: &str) -> &'static str {
    match path.rsplit('.').next().unwrap_or("").to_ascii_lowercase().as_str() {
        "gif" => "image/gif",
        "png" => "image/png",
        "jpg" | "jpeg" => "image/jpeg",
        "webp" => "image/webp",
        "svg" => "image/svg+xml",
        "md" => "text/markdown; charset=utf-8",
        "json" => "application/json",
        _ => "application/octet-stream",
    }
}

fn serve(app: &AppHandle, path: &str) -> tauri::http::Response<Vec<u8>> {
    let rel = unescape(path);
    let res = current(app).and_then(|c| c.file(&rel));
    let b = tauri::http::Response::builder().header("Access-Control-Allow-Origin", "*");
    match res {
        Ok(bytes) => b.status(200).header("Content-Type", mime(&rel)).body(bytes),
        Err(e) => b.status(404).header("Content-Type", "text/plain").body(e.into_bytes()),
    }
    .unwrap()
}

// ---------------------------------------------------------------- the command line (for testing)

fn cli() -> Option<i32> {
    let args: Vec<String> = std::env::args().skip(1).collect();
    match args.first().map(|s| s.as_str()) {
        Some("--boards") => {
            let seen = boards::Seen::default();
            let list = boards::list(&[], &seen, &HashMap::new());
            println!("{}", serde_json::to_string_pretty(&list).unwrap());
            Some(0)
        }
        Some("--check") => {
            // reboot a running board into download mode, read its MAC and restart it: no writing
            let seen = boards::Seen::default();
            let list = boards::list(&[], &seen, &HashMap::new());
            let Some(b) = list.into_iter().find(|b| args.get(1).map_or(true, |k| &b.key == k || b.port.contains(k.as_str()))) else {
                eprintln!("no board");
                return Some(1);
            };
            let port = if b.mode == "running" {
                match reboot_to_download(&b.port, b.mac_tail.as_deref()) {
                    Ok(p) => p,
                    Err(e) => {
                        eprintln!("{e}");
                        return Some(1);
                    }
                }
            } else {
                b.port.clone()
            };
            match flash::check(&port) {
                Ok(mac) => {
                    println!("{} on {port}: {mac}, restarted", b.name.unwrap_or_default());
                    Some(0)
                }
                Err(e) => {
                    eprintln!("{e}");
                    Some(1)
                }
            }
        }
        Some("--help") | Some("-h") => {
            println!("samplooler-library            the window\n  --boards                      list the boards on this computer\n  --check [key]                 restart a board into download mode, read its MAC, restart it (writes nothing)\n\nSAMPLOOLER_LIBRARY=<url or folder> reads the library from there.");
            Some(0)
        }
        _ => None,
    }
}

fn main() {
    if let Some(code) = cli() {
        std::process::exit(code);
    }
    // WebKitGTK's DMA-BUF renderer dies on Wayland with NVIDIA ("Error 71 (Protocol error)"); without
    // it the window draws everywhere. A value set by the user stays.
    #[cfg(target_os = "linux")]
    if std::env::var_os("WEBKIT_DISABLE_DMABUF_RENDERER").is_none() {
        std::env::set_var("WEBKIT_DISABLE_DMABUF_RENDERER", "1");
    }
    tauri::Builder::default()
        .plugin(tauri_plugin_dialog::init())
        .plugin(tauri_plugin_opener::init())
        .manage(App::default())
        .setup(|app| {
            let h = app.handle();
            if let Ok(b) = std::fs::read(config_path(h)) {
                if let Ok(c) = serde_json::from_slice::<Config>(&b) {
                    *h.state::<App>().config.lock().unwrap() = c;
                }
            }
            Ok(())
        })
        .register_asynchronous_uri_scheme_protocol("lib", |ctx, request, responder| {
            let app = ctx.app_handle().clone();
            let path = request.uri().path().to_string();
            std::thread::spawn(move || responder.respond(serve(&app, &path)));
        })
        .invoke_handler(tauri::generate_handler![
            library,
            set_source,
            settings,
            clear_cache,
            boards,
            install,
            inspect_build,
            line_open,
            line_close,
            line_set,
            line_cc,
            line_describe,
            line_status,
            line_ping,
        ])
        .run(tauri::generate_context!())
        .expect("the window did not start");
}
