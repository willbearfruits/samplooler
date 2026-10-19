//! The boards on this computer. A board running one of ours shows up as a MIDI port named after its
//! firmware ("SING", "Samplooler"); a board in the ROM's download mode shows up as Espressif's
//! USB-Serial-JTAG port (303a:1001), whose USB serial number is the chip's MAC. A TWOFOLD is never one
//! of them: its name is never matched, so nothing here opens its MIDI port, and its USB id (303a:4a57)
//! is never a download-mode port.

use crate::midi;
use serde::Serialize;
use std::collections::HashMap;
use std::sync::Mutex;

pub const ESPRESSIF_VID: u16 = 0x303a;
pub const USJ_PID: u16 = 0x1001;
pub const NEVER: &[&str] = &["TWOFOLD"];
/// Names the firmwares had before the catalog knew them, and the ones it may not list.
pub const LEGACY: &[&str] = &[
    "Samplooler", "Samplooler Family", "KNOCK", "HUFF", "HOWL", "SNIFF", "CHIRP", "ETHER", "JAM", "BAND",
    "SING", "WRECK", "MANTRA", "DEMO",
];

#[derive(Serialize, Clone, Debug)]
pub struct Board {
    /// "midi:<port>" for a running board, "dl:<serial port>" for one in download mode
    pub key: String,
    /// "running" or "download"
    pub mode: &'static str,
    /// the firmware it runs, as its MIDI port names it
    pub name: Option<String>,
    /// the MIDI port's full name, or the serial port
    pub port: String,
    /// its USB serial: "SG-727600D0" while running, the MAC in download mode
    pub serial: Option<String>,
    /// the last four bytes of the MAC, "72:76:00:D0", when known
    pub mac_tail: Option<String>,
    /// what it says it runs (protocol 1 and up), else what this app last installed on it
    pub version: Option<String>,
    /// 0: the firmware does not answer the identify message (older builds)
    pub proto: u32,
}

/// What a running board said when asked, kept per MIDI port while it stays plugged in.
#[derive(Default)]
pub struct Seen {
    ids: Mutex<HashMap<String, midi::Identity>>,
}

/// "SING:SING MIDI 1 28:0" (ALSA), "2- SING" (Windows), "SING" (macOS) -> "SING"
pub fn port_label(port: &str) -> String {
    let mut s = port;
    if let Some((client, _)) = s.split_once(':') {
        s = client;
    }
    let s = s.trim();
    if let Some((n, rest)) = s.split_once("- ") {
        if n.chars().all(|c| c.is_ascii_digit()) {
            return rest.trim().to_string();
        }
    }
    s.to_string()
}

/// The firmware name a MIDI port belongs to, if it is one of ours.
pub fn family_name(port: &str, names: &[String]) -> Option<String> {
    let label = port_label(port);
    if NEVER.iter().any(|n| label.to_uppercase().contains(n)) {
        return None;
    }
    let all = names.iter().map(|s| s.as_str()).chain(LEGACY.iter().copied());
    // the longest match first: "Samplooler Family" before "Samplooler"
    let mut best: Option<&str> = None;
    for n in all {
        let hit = label.eq_ignore_ascii_case(n)
            || label.to_uppercase().starts_with(&(n.to_uppercase() + " "))
            || label.to_uppercase().starts_with(&(n.to_uppercase() + "_"));
        if hit && best.map_or(true, |b| n.len() > b.len()) {
            best = Some(n);
        }
    }
    best.map(|n| n.to_string())
}

/// "SG-727600D0" -> "72:76:00:D0"; "90:DA:72:76:00:D0" -> "72:76:00:D0"
pub fn mac_tail(serial: &str) -> Option<String> {
    let hex: String = serial.chars().filter(|c| c.is_ascii_hexdigit()).collect();
    let hex = hex.to_uppercase();
    if hex.len() < 8 {
        return None;
    }
    let t = &hex[hex.len() - 8..];
    Some((0..4).map(|i| &t[i * 2..i * 2 + 2]).collect::<Vec<_>>().join(":"))
}

/// The download-mode ports: (port, MAC).
pub fn download_ports() -> Vec<(String, String)> {
    let mut out = vec![];
    if let Ok(ports) = serialport::available_ports() {
        for p in ports {
            if let serialport::SerialPortType::UsbPort(u) = &p.port_type {
                if u.vid == ESPRESSIF_VID && u.pid == USJ_PID {
                    // macOS lists each port twice, as /dev/cu.* and /dev/tty.*: keep the cu one
                    if p.port_name.starts_with("/dev/tty.") {
                        continue;
                    }
                    out.push((p.port_name.clone(), u.serial_number.clone().unwrap_or_default()));
                }
            }
        }
    }
    out.sort();
    out
}

/// Linux only: the USB serial of the card whose MIDI port has this name (for firmwares that do not
/// answer the identify message). Several cards of the same name: none, the identify answer is needed.
#[cfg(target_os = "linux")]
fn linux_serial(label: &str) -> Option<String> {
    let mut hits = vec![];
    for n in 0..32 {
        let Ok(first) = std::fs::read_to_string(format!("/proc/asound/card{n}/midi0")) else { continue };
        let first = first.lines().next().unwrap_or("").trim().to_string();
        if first == label {
            if let Ok(s) = std::fs::read_to_string(format!("/sys/class/sound/card{n}/device/../serial")) {
                hits.push(s.trim().to_string());
            }
        }
    }
    if hits.len() == 1 { hits.pop() } else { None }
}
#[cfg(not(target_os = "linux"))]
fn linux_serial(_: &str) -> Option<String> {
    None
}

/// Every board of ours on this computer. Running boards seen for the first time are asked who they are.
pub fn list(names: &[String], seen: &Seen, installed: &HashMap<String, (String, String)>) -> Vec<Board> {
    let mut out = vec![];
    let ports = midi::output_ports();
    {
        let mut ids = seen.ids.lock().unwrap();
        ids.retain(|p, _| ports.contains(p));
    }
    for port in &ports {
        let Some(name) = family_name(port, names) else { continue };
        let known = seen.ids.lock().unwrap().get(port).cloned();
        let id = match known {
            Some(i) => i,
            None => {
                let i = midi::identify(port).unwrap_or_default();
                seen.ids.lock().unwrap().insert(port.clone(), i.clone());
                i
            }
        };
        let serial = id.serial.clone().or_else(|| linux_serial(&port_label(port)));
        let tail = serial.as_deref().and_then(mac_tail);
        let version = id.version.clone().or_else(|| {
            let t = tail.as_ref()?;
            let (iid, ver) = installed.get(t)?;
            // what we installed, as long as it still runs it
            if iid.eq_ignore_ascii_case(&name) || names_match(iid, &name) { Some(ver.clone()) } else { None }
        });
        out.push(Board {
            key: format!("midi:{port}"),
            mode: "running",
            name: Some(id.name.clone().unwrap_or(name)),
            port: port.clone(),
            serial,
            mac_tail: tail,
            version,
            proto: id.proto,
        });
    }
    for (port, mac) in download_ports() {
        out.push(Board {
            key: format!("dl:{port}"),
            mode: "download",
            name: None,
            port,
            mac_tail: mac_tail(&mac),
            serial: if mac.is_empty() { None } else { Some(mac) },
            version: None,
            proto: 0,
        });
    }
    out
}

fn names_match(id: &str, name: &str) -> bool {
    id.replace(['-', '_'], " ").eq_ignore_ascii_case(name)
}

/// Forget what a port said, so the next listing asks again (after a flash).
pub fn forget(seen: &Seen) {
    seen.ids.lock().unwrap().clear();
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn labels() {
        assert_eq!(port_label("SING:SING MIDI 1 28:0"), "SING");
        assert_eq!(port_label("2- SING"), "SING");
        assert_eq!(port_label("Samplooler"), "Samplooler");
    }

    #[test]
    fn family() {
        let names = vec!["SING".to_string(), "WRECK".to_string()];
        assert_eq!(family_name("SING:SING MIDI 1 28:0", &names).as_deref(), Some("SING"));
        assert_eq!(family_name("TWOFOLD:TWOFOLD MIDI 1 24:0", &names), None);
        assert_eq!(family_name("Samplooler Family:x 20:0", &names).as_deref(), Some("Samplooler Family"));
        assert_eq!(family_name("Samplooler:Samplooler MIDI 1 20:0", &names).as_deref(), Some("Samplooler"));
        assert_eq!(family_name("Midi Through:Midi Through Port-0 14:0", &names), None);
        assert_eq!(family_name("SINGER:x 20:0", &names), None);
    }

    #[test]
    fn macs() {
        assert_eq!(mac_tail("SG-727600D0").as_deref(), Some("72:76:00:D0"));
        assert_eq!(mac_tail("90:DA:72:76:00:D0").as_deref(), Some("72:76:00:D0"));
        assert_eq!(mac_tail("x"), None);
    }
}
