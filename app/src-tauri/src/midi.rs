//! Talking to a running board over its MIDI port. Our SysEx messages start F0 7D (the
//! non-commercial id) and a letter; the text in them is 7-bit ASCII, one `key=value` per line.
//!
//!   F0 7D 52 F7            'R'  reboot into the ROM's download mode (every firmware of ours)
//!   F0 7D 4C <text> F7     'L'  a log line, from the board
//!   F0 7D 49 F7            'I'  who are you: the board answers F0 7D 49 name= version= serial= proto= params=
//!   F0 7D 44 F7            'D'  describe the settings: one F0 7D 44 <one setting> F7 each, then end=<count>
//!   F0 7D 50 tag=value F7  'P'  set one; the board answers with 'V'. A kind=step setting (the Samplooler's)
//!                               takes +1 or -1, as a turn of its encoder; a kind=act one runs at 1
//!   F0 7D 56 <text> F7     'V'  a setting's value, from the board: tag= value= text=
//!
//! While the window shows a board it says 'I' every 2 s: a board stops sending 'V' after 5 s of silence and
//! catches up when it hears from the window again.
//!
//! 'I', 'D', 'P' and 'V' are protocol 1. A firmware that does not answer 'I' still reboots on 'R'.

use midir::{MidiInput, MidiInputConnection, MidiOutput, MidiOutputConnection};
use serde::Serialize;
use std::collections::BTreeMap;
use std::sync::mpsc;
use std::time::Duration;

const CLIENT: &str = "Samplooler Library";

pub const REBOOT: [u8; 4] = [0xF0, 0x7D, b'R', 0xF7];
pub const IDENTIFY: [u8; 4] = [0xF0, 0x7D, b'I', 0xF7];
pub const DESCRIBE: [u8; 4] = [0xF0, 0x7D, b'D', 0xF7];
pub const STATUS: [u8; 4] = [0xF0, 0x7D, b'S', 0xF7];

#[derive(Serialize, Clone, Debug, Default)]
pub struct Identity {
    pub name: Option<String>,
    pub version: Option<String>,
    pub serial: Option<String>,
    pub proto: u32,
    pub params: Option<u32>,
}

/// A message from the board, as the window gets it.
#[derive(Serialize, Clone, Debug)]
#[serde(tag = "kind", rename_all = "lowercase")]
pub enum Incoming {
    /// a log line
    Log { text: String },
    /// one setting described ('D'), or the end of the list (`end` set)
    Param { fields: BTreeMap<String, String> },
    /// a setting's value ('V')
    Value { fields: BTreeMap<String, String> },
    /// a control change (the Samplooler's settings, the telemetry)
    Cc { channel: u8, cc: u8, value: u8 },
    /// a note on (velocity > 0) or off
    Note { note: u8, on: bool },
}

pub fn output_ports() -> Vec<String> {
    let Ok(out) = MidiOutput::new(CLIENT) else { return vec![] };
    out.ports().iter().filter_map(|p| out.port_name(p).ok()).collect()
}

fn find_out(name: &str) -> Result<MidiOutputConnection, String> {
    let out = MidiOutput::new(CLIENT).map_err(|e| e.to_string())?;
    let port = out
        .ports()
        .into_iter()
        .find(|p| out.port_name(p).ok().as_deref() == Some(name))
        .ok_or_else(|| format!("{name} is not there any more"))?;
    out.connect(&port, "out").map_err(|e| e.to_string())
}

fn find_in<F>(name: &str, f: F) -> Result<MidiInputConnection<()>, String>
where
    F: FnMut(u64, &[u8], &mut ()) + Send + 'static,
{
    let input = MidiInput::new(CLIENT).map_err(|e| e.to_string())?;
    // the input port of a board has the same name as its output port
    let port = input
        .ports()
        .into_iter()
        .find(|p| input.port_name(p).ok().as_deref() == Some(name))
        .ok_or_else(|| format!("{name} has no input port"))?;
    input.connect(&port, "in", f, ()).map_err(|e| e.to_string())
}

pub fn send(port: &str, bytes: &[u8]) -> Result<(), String> {
    let mut c = find_out(port)?;
    c.send(bytes).map_err(|e| e.to_string())?;
    // let the message leave before the port closes
    std::thread::sleep(Duration::from_millis(30));
    Ok(())
}

/// The `key=value` lines of a SysEx message's text.
pub fn fields(text: &[u8]) -> BTreeMap<String, String> {
    let mut m = BTreeMap::new();
    for line in String::from_utf8_lossy(text).split('\n') {
        if let Some((k, v)) = line.split_once('=') {
            m.insert(k.trim().to_string(), v.to_string());
        }
    }
    m
}

pub fn parse_identity(text: &[u8]) -> Identity {
    let f = fields(text);
    Identity {
        name: f.get("name").cloned(),
        version: f.get("version").cloned(),
        serial: f.get("serial").cloned(),
        proto: f.get("proto").and_then(|s| s.parse().ok()).unwrap_or(1),
        params: f.get("params").and_then(|s| s.parse().ok()),
    }
}

/// Ask a running board who it is. A firmware that does not know the message stays silent: proto 0.
pub fn identify(port: &str) -> Result<Identity, String> {
    let (tx, rx) = mpsc::channel();
    let _in = find_in(port, move |_, msg, _| {
        if msg.len() > 3 && msg[0] == 0xF0 && msg[1] == 0x7D && msg[2] == b'I' {
            let _ = tx.send(parse_identity(&msg[3..msg.len() - 1]));
        }
    })?;
    let mut out = find_out(port)?;
    out.send(&IDENTIFY).map_err(|e| e.to_string())?;
    Ok(rx.recv_timeout(Duration::from_millis(400)).unwrap_or_default())
}

pub fn decode(msg: &[u8]) -> Option<Incoming> {
    if msg.len() >= 4 && msg[0] == 0xF0 && msg[1] == 0x7D && *msg.last()? == 0xF7 {
        let body = &msg[3..msg.len() - 1];
        return match msg[2] {
            b'L' => Some(Incoming::Log { text: String::from_utf8_lossy(body).trim_end().to_string() }),
            b'D' => Some(Incoming::Param { fields: fields(body) }),
            b'V' => Some(Incoming::Value { fields: fields(body) }),
            _ => None,
        };
    }
    match (msg.first()? & 0xF0, msg.len()) {
        (0xB0, 3) => Some(Incoming::Cc { channel: msg[0] & 0x0F, cc: msg[1], value: msg[2] }),
        (0x90, 3) => Some(Incoming::Note { note: msg[1], on: msg[2] > 0 }),
        (0x80, 3) => Some(Incoming::Note { note: msg[1], on: false }),
        _ => None,
    }
}

/// 'P' tag=value, 7-bit: a byte of 0x80 or more would end the SysEx early.
pub fn set_message(tag: &str, value: &str) -> Vec<u8> {
    let mut m = vec![0xF0, 0x7D, b'P'];
    m.extend(format!("{tag}={value}").bytes().filter(|b| *b < 0x80));
    m.push(0xF7);
    m
}

/// An open line to one board, for the settings and its log. Dropping it closes both ports.
pub struct Line {
    pub port: String,
    out: MidiOutputConnection,
    _in: MidiInputConnection<()>,
}

impl Line {
    pub fn open<F>(port: &str, mut on: F) -> Result<Line, String>
    where
        F: FnMut(Incoming) + Send + 'static,
    {
        let input = find_in(port, move |_, msg, _| {
            if let Some(m) = decode(msg) {
                on(m)
            }
        })?;
        Ok(Line { port: port.to_string(), out: find_out(port)?, _in: input })
    }

    pub fn send(&mut self, bytes: &[u8]) -> Result<(), String> {
        self.out.send(bytes).map_err(|e| e.to_string())
    }

    /// 'P': set a setting by its tag.
    pub fn set(&mut self, tag: &str, value: &str) -> Result<(), String> {
        self.send(&set_message(tag, value))
    }

    pub fn cc(&mut self, channel: u8, cc: u8, value: u8) -> Result<(), String> {
        self.send(&[0xB0 | (channel & 0x0F), cc & 0x7F, value & 0x7F])
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn identity() {
        let i = parse_identity(b"name=SING\nversion=0.2.0\nserial=SG-727600D0\nproto=1\nparams=63");
        assert_eq!(i.name.as_deref(), Some("SING"));
        assert_eq!(i.version.as_deref(), Some("0.2.0"));
        assert_eq!(i.proto, 1);
        assert_eq!(i.params, Some(63));
    }

    #[test]
    fn decoding() {
        match decode(&[0xF0, 0x7D, b'V', b't', b'a', b'g', b'=', b'G', b'A', b'I', b'N', 0xF7]) {
            Some(Incoming::Value { fields }) => assert_eq!(fields["tag"], "GAIN"),
            x => panic!("{x:?}"),
        }
        assert!(matches!(decode(&[0xB2, 20, 64]), Some(Incoming::Cc { channel: 2, cc: 20, value: 64 })));
        assert!(matches!(decode(&[0x90, 60, 0]), Some(Incoming::Note { note: 60, on: false })));
    }

    #[test]
    fn setting() {
        assert_eq!(set_message("p03", "-1"), [0xF0, 0x7D, b'P', b'p', b'0', b'3', b'=', b'-', b'1', 0xF7]);
        assert_eq!(set_message("vol ", "80"), b"\xF0\x7DPvol =80\xF7".to_vec());
        assert_eq!(set_message("x\u{e9}", "1"), [0xF0, 0x7D, b'P', b'x', b'=', b'1', 0xF7]);
    }

    #[test]
    fn describing() {
        // a step setting of the Samplooler's, as its firmware describes it
        match decode(b"\xF0\x7DDi=1\ntag=p01\nname=Mode\ngroup=PLAY\nkind=step\ntext=TAPE\nhelp=TAPE: the pitch follows the speed.\xF7") {
            Some(Incoming::Param { fields }) => {
                assert_eq!(fields["kind"], "step");
                assert_eq!(fields["text"], "TAPE");
                assert_eq!(fields["help"], "TAPE: the pitch follows the speed.");
            }
            x => panic!("{x:?}"),
        }
        match decode(b"\xF0\x7DDend=23\xF7") {
            Some(Incoming::Param { fields }) => assert_eq!(fields["end"], "23"),
            x => panic!("{x:?}"),
        }
    }
}
