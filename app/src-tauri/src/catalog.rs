//! The library: `library.json` and the files next to it, as `tools/library.py site` writes them, either
//! on the web (the project page) or in a folder on this computer. What comes from the web is kept in the
//! cache, so a firmware once downloaded installs again without a connection. Firmware parts are kept by
//! their SHA-256 and checked against it; the rest is kept per instrument and version.

use serde_json::Value;
use sha2::{Digest, Sha256};
use std::io::Read;
use std::path::{Path, PathBuf};
use std::time::Duration;

pub const DEFAULT_SOURCE: &str = "https://willbearfruits.github.io/samplooler/";

#[derive(Clone, Debug)]
pub enum Source {
    Web(String),
    Folder(PathBuf),
}

impl Source {
    pub fn parse(s: &str) -> Source {
        let s = s.trim();
        if s.starts_with("http://") || s.starts_with("https://") {
            Source::Web(if s.ends_with('/') { s.to_string() } else { format!("{s}/") })
        } else {
            let p = PathBuf::from(s);
            // a path to library.json itself is fine too
            if p.file_name().map_or(false, |n| n == "library.json") {
                Source::Folder(p.parent().unwrap_or(Path::new(".")).to_path_buf())
            } else {
                Source::Folder(p)
            }
        }
    }
    pub fn describe(&self) -> String {
        match self {
            Source::Web(u) => u.clone(),
            Source::Folder(p) => p.display().to_string(),
        }
    }
}

pub struct Catalog {
    pub source: Source,
    pub cache: PathBuf,
    /// library.json as read
    pub data: Value,
    /// read from the cache because the web could not be reached
    pub offline: bool,
}

fn agent() -> ureq::Agent {
    ureq::AgentBuilder::new()
        .timeout_connect(Duration::from_secs(6))
        .timeout_read(Duration::from_secs(30))
        .user_agent(concat!("samplooler-library/", env!("CARGO_PKG_VERSION")))
        .build()
}

/// GET a URL into memory; `on` gets (bytes so far, total if known).
pub fn fetch(url: &str, mut on: impl FnMut(usize, Option<usize>)) -> Result<Vec<u8>, String> {
    let r = agent().get(url).call().map_err(|e| match e {
        ureq::Error::Status(code, _) => format!("{url}: {code}"),
        e => format!("{url}: {e}"),
    })?;
    let total = r.header("content-length").and_then(|s| s.parse().ok());
    let mut rd = r.into_reader().take(64 << 20);
    let mut buf = Vec::new();
    let mut chunk = [0u8; 16384];
    loop {
        let n = rd.read(&mut chunk).map_err(|e| format!("{url}: {e}"))?;
        if n == 0 {
            break;
        }
        buf.extend_from_slice(&chunk[..n]);
        on(buf.len(), total);
    }
    Ok(buf)
}

pub fn sha256(data: &[u8]) -> String {
    Sha256::digest(data).iter().map(|b| format!("{b:02x}")).collect()
}

fn write_atomic(path: &Path, data: &[u8]) -> Result<(), String> {
    if let Some(d) = path.parent() {
        std::fs::create_dir_all(d).map_err(|e| e.to_string())?;
    }
    let tmp = path.with_extension("part");
    std::fs::write(&tmp, data).map_err(|e| e.to_string())?;
    std::fs::rename(&tmp, path).map_err(|e| e.to_string())
}

/// A relative path from library.json or a request, kept inside the library: no "..", no absolute paths.
pub fn clean(rel: &str) -> Option<String> {
    let rel = rel.trim_start_matches('/');
    let parts: Vec<&str> = rel.split('/').filter(|s| !s.is_empty() && *s != ".").collect();
    if parts.is_empty() || parts.iter().any(|s| *s == ".." || s.contains('\\') || s.contains(':')) {
        return None;
    }
    Some(parts.join("/"))
}

impl Catalog {
    pub fn load(source: Source, cache: PathBuf) -> Result<Catalog, String> {
        let (data, offline) = match &source {
            Source::Folder(dir) => {
                let p = dir.join("library.json");
                let s = std::fs::read(&p).map_err(|e| format!("{}: {e}", p.display()))?;
                (serde_json::from_slice(&s).map_err(|e| format!("{}: {e}", p.display()))?, false)
            }
            Source::Web(base) => {
                let kept = cache.join("library.json");
                match fetch(&format!("{base}library.json"), |_, _| {}) {
                    Ok(bytes) => {
                        let v: Value = serde_json::from_slice(&bytes).map_err(|e| format!("library.json: {e}"))?;
                        let _ = write_atomic(&kept, &bytes);
                        (v, false)
                    }
                    Err(e) => {
                        let s = std::fs::read(&kept).map_err(|_| format!("no connection and nothing kept yet ({e})"))?;
                        (serde_json::from_slice(&s).map_err(|e| e.to_string())?, true)
                    }
                }
            }
        };
        if !data["entries"].is_array() {
            return Err("library.json has no entries".into());
        }
        Ok(Catalog { source, cache, data, offline })
    }

    pub fn entries(&self) -> Vec<Value> {
        self.data["entries"].as_array().cloned().unwrap_or_default()
    }

    pub fn entry(&self, id: &str) -> Option<Value> {
        self.entries().into_iter().find(|e| e["id"] == id)
    }

    fn version_of(&self, id: &str) -> String {
        self.entry(id)
            .and_then(|e| e["version"].as_str().map(|s| s.to_string()))
            .unwrap_or_else(|| "_".into())
    }

    /// A file of the library, by its path in it ("sing/images/sing-voices.gif"): from the folder, or
    /// from the cache, or from the web (then kept).
    pub fn file(&self, rel: &str) -> Result<Vec<u8>, String> {
        let rel = clean(rel).ok_or("bad path")?;
        match &self.source {
            Source::Folder(dir) => std::fs::read(dir.join(&rel)).map_err(|e| format!("{rel}: {e}")),
            Source::Web(base) => {
                let id = rel.split('/').next().unwrap_or("_");
                let kept = self.cache.join("files").join(id).join(self.version_of(id)).join(&rel);
                if let Ok(b) = std::fs::read(&kept) {
                    return Ok(b);
                }
                let b = fetch(&format!("{base}{rel}"), |_, _| {})?;
                let _ = write_atomic(&kept, &b);
                Ok(b)
            }
        }
    }

    /// The parts of an entry's firmware: [{path, offset, sha256?, size?}].
    pub fn parts(&self, id: &str) -> Result<Vec<Value>, String> {
        let e = self.entry(id).ok_or_else(|| format!("{id} is not in the library"))?;
        if let Some(p) = e["firmware"]["parts"].as_array() {
            return Ok(p.iter().map(|x| {
                let mut x = x.clone();
                if let Some(path) = x["path"].as_str() {
                    x["path"] = Value::String(format!("{id}/{path}"));
                }
                x
            }).collect());
        }
        // an older library.json: only the web flasher's manifest
        let m = e["manifest"].as_str().ok_or_else(|| format!("{id} has no firmware in the library"))?;
        let manifest: Value = serde_json::from_slice(&self.file(m)?).map_err(|e| e.to_string())?;
        let dir = m.rsplit_once('/').map(|x| x.0).unwrap_or("");
        Ok(manifest["builds"][0]["parts"]
            .as_array()
            .cloned()
            .unwrap_or_default()
            .into_iter()
            .map(|p| serde_json::json!({ "path": format!("{dir}/{}", p["path"].as_str().unwrap_or("")), "offset": p["offset"] }))
            .collect())
    }

    fn kept_part(&self, p: &Value) -> Option<PathBuf> {
        p["sha256"].as_str().map(|s| self.cache.join("firmware").join(format!("{s}.bin")))
    }

    /// Whether every part of an entry's firmware is on this computer already.
    pub fn has_firmware(&self, id: &str) -> bool {
        if let Source::Folder(_) = self.source {
            return true;
        }
        let Some(parts) = self.entry(id).and_then(|e| e["firmware"]["parts"].as_array().cloned()) else { return false };
        !parts.is_empty() && parts.iter().all(|p| self.kept_part(p).map_or(false, |k| k.is_file()))
    }

    /// The firmware's parts as bytes, downloaded if needed and checked. `on` gets (bytes, total bytes).
    pub fn firmware(&self, id: &str, mut on: impl FnMut(usize, usize)) -> Result<Vec<crate::flash::Part>, String> {
        let parts = self.parts(id)?;
        if parts.is_empty() {
            return Err(format!("{id} has no firmware in the library"));
        }
        let total: usize = parts.iter().map(|p| p["size"].as_u64().unwrap_or(0) as usize).sum();
        let mut out = vec![];
        let mut done = 0usize;
        for p in &parts {
            let path = p["path"].as_str().ok_or("a part without a path")?;
            let offset = p["offset"].as_u64().ok_or("a part without an offset")? as u32;
            let want = p["sha256"].as_str();
            let kept = self.kept_part(p);
            let mut data = kept.as_ref().and_then(|k| std::fs::read(k).ok()).filter(|d| want.map_or(true, |w| sha256(d) == w));
            if data.is_none() {
                let rel = clean(path).ok_or("bad path")?;
                let d = match &self.source {
                    Source::Folder(dir) => std::fs::read(dir.join(&rel)).map_err(|e| format!("{rel}: {e}"))?,
                    Source::Web(base) => {
                        let base_done = done;
                        fetch(&format!("{base}{rel}"), |n, _| on(base_done + n, total.max(base_done + n)))?
                    }
                };
                if let Some(w) = want {
                    if sha256(&d) != w {
                        return Err(format!("{rel} did not download whole (its checksum is wrong): try again"));
                    }
                }
                if let (Some(k), Source::Web(_)) = (&kept, &self.source) {
                    let _ = write_atomic(k, &d);
                }
                data = Some(d);
            }
            let data = data.unwrap();
            done += data.len();
            on(done, total.max(done));
            let name = path.rsplit('/').next().unwrap_or(path).to_string();
            out.push(crate::flash::Part { offset, name, data });
        }
        Ok(out)
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn paths() {
        assert_eq!(clean("/sing/images/a.gif").as_deref(), Some("sing/images/a.gif"));
        assert_eq!(clean("sing/../../etc/passwd"), None);
        assert_eq!(clean("C:/x"), None);
        assert_eq!(clean(""), None);
    }

    #[test]
    fn sources() {
        assert!(matches!(Source::parse("https://x.io/lib"), Source::Web(u) if u == "https://x.io/lib/"));
        assert!(matches!(Source::parse("/tmp/_site/library.json"), Source::Folder(p) if p == PathBuf::from("/tmp/_site")));
    }
}
