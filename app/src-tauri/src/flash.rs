//! Writing a firmware onto a board in download mode, with the espflash crate. Only the parts a build
//! names are written (bootloader at 0x0, partition table at 0x8000, the program at 0x10000): the NVS
//! partition between them, where every firmware keeps its settings and saved sounds, and the Samplooler's
//! sample slots further up, are left alone. Afterwards the board is restarted by its RTC watchdog,
//! as tools/flash.sh does with esptool: a board that entered download mode from our SysEx comes back
//! running.

use espflash::connection::{Connection, ResetAfterOperation, ResetBeforeOperation};
use espflash::flasher::Flasher;
use espflash::image_format::Segment;
use espflash::target::{Chip, ProgressCallbacks};
use serialport::{FlowControl, SerialPortType, UsbPortInfo};
use std::borrow::Cow;

pub struct Part {
    pub offset: u32,
    pub name: String,
    pub data: Vec<u8>,
}

/// Progress as the window shows it.
pub enum Step {
    Connected { mac: String },
    Writing { part: usize, done: usize, total: usize },
    Verifying { part: usize },
    Restarting,
}

struct Progress<'a, F: FnMut(Step)> {
    on: &'a mut F,
    part: usize,
    total: usize,
    started: bool,
}

impl<F: FnMut(Step)> ProgressCallbacks for Progress<'_, F> {
    fn init(&mut self, _addr: u32, total: usize) {
        if self.started {
            self.part += 1;
        }
        self.started = true;
        self.total = total;
        (self.on)(Step::Writing { part: self.part, done: 0, total });
    }
    fn update(&mut self, current: usize) {
        (self.on)(Step::Writing { part: self.part, done: current, total: self.total });
    }
    fn verifying(&mut self) {
        (self.on)(Step::Verifying { part: self.part });
    }
    fn finish(&mut self, _skipped: bool) {
        (self.on)(Step::Writing { part: self.part, done: self.total, total: self.total });
    }
}

fn port_info(port: &str) -> UsbPortInfo {
    serialport::available_ports()
        .unwrap_or_default()
        .into_iter()
        .find(|p| p.port_name == port)
        .and_then(|p| match p.port_type {
            SerialPortType::UsbPort(u) => Some(u),
            _ => None,
        })
        .unwrap_or(UsbPortInfo {
            vid: crate::boards::ESPRESSIF_VID,
            pid: crate::boards::USJ_PID,
            serial_number: None,
            manufacturer: None,
            product: None,
        })
}

fn connect(port: &str) -> Result<Flasher, String> {
    let info = port_info(port);
    if info.vid != crate::boards::ESPRESSIF_VID || info.pid != crate::boards::USJ_PID {
        return Err(format!("{port} is not a board in download mode"));
    }
    let serial = serialport::new(port, 115_200)
        .flow_control(FlowControl::None)
        .open_native()
        .map_err(|e| format!("could not open {port}: {e}"))?;
    let conn = Connection::new(serial, info, ResetAfterOperation::NoResetNoStub, ResetBeforeOperation::DefaultReset, 115_200);
    Flasher::connect(conn, true, true, false, Some(Chip::Esp32s3), None).map_err(|e| format!("could not talk to the board: {e}"))
}

/// The board's MAC, "90:DA:72:76:00:D0".
fn mac(flasher: &mut Flasher) -> Result<String, String> {
    let chip = flasher.chip();
    chip.mac_address(flasher.connection()).map(|m| m.to_uppercase()).map_err(|e| e.to_string())
}

fn restart(flasher: Flasher) -> Result<(), String> {
    let chip = flasher.chip();
    let mut conn = flasher.into_connection();
    chip.rtc_wdt_reset(&mut conn).map_err(|e| e.to_string())
}

/// Write `parts` to the board on `port`. `want_tail` (the MAC's last four bytes) makes sure it is the
/// board that was asked to reboot, not another one that happens to be in download mode.
pub fn write<F: FnMut(Step)>(port: &str, parts: &[Part], want_tail: Option<&str>, mut on: F) -> Result<String, String> {
    let mut flasher = connect(port)?;
    let mac = mac(&mut flasher)?;
    if let Some(t) = want_tail {
        if !mac.ends_with(&t.to_uppercase()) {
            return Err(format!("the board on {port} is {mac}, not the one that was asked to reboot ({t})"));
        }
    }
    on(Step::Connected { mac: mac.clone() });
    let segments: Vec<Segment> = parts
        .iter()
        .map(|p| {
            let mut data = p.data.clone();
            while data.len() % 4 != 0 {
                data.push(0xFF);
            }
            Segment { addr: p.offset, data: Cow::Owned(data) }
        })
        .collect();
    let mut progress = Progress { on: &mut on, part: 0, total: 0, started: false };
    flasher.write_bins_to_flash(&segments, &mut progress).map_err(|e| format!("writing failed: {e}"))?;
    on(Step::Restarting);
    restart(flasher)?;
    Ok(mac)
}

/// Connect, read the MAC and restart: everything but writing (`--check` on the command line).
pub fn check(port: &str) -> Result<String, String> {
    let mut flasher = connect(port)?;
    let mac = mac(&mut flasher)?;
    restart(flasher)?;
    Ok(mac)
}
