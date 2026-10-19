//! A build on this computer: an ESP-IDF build folder (or the project, or the instrument's folder above
//! it), flashed from the parts its flash_args names.

use crate::flash::Part;
use serde::Serialize;
use std::path::{Path, PathBuf};

#[derive(Serialize, Clone, Debug)]
pub struct Build {
    pub dir: String,
    pub name: String,
    pub version: String,
    pub parts: Vec<(u32, String, u64)>,
}

/// The build folder under `p`: p itself, p/build or p/firmware/build.
fn build_dir(p: &Path) -> Option<PathBuf> {
    [p.to_path_buf(), p.join("build"), p.join("firmware").join("build")]
        .into_iter()
        .find(|d| d.join("flash_args").is_file())
}

pub fn parse_flash_args(text: &str) -> Vec<(u32, String)> {
    let mut parts = vec![];
    for line in text.lines() {
        let mut w = line.split_whitespace();
        if let (Some(off), Some(path)) = (w.next(), w.next()) {
            if let Some(hex) = off.strip_prefix("0x") {
                if let Ok(o) = u32::from_str_radix(hex, 16) {
                    parts.push((o, path.to_string()));
                }
            }
        }
    }
    parts.sort();
    parts
}

pub fn inspect(p: &str) -> Result<Build, String> {
    let dir = build_dir(Path::new(p)).ok_or("no build here: pick a folder with a finished ESP-IDF build (build/flash_args)")?;
    let parts = parse_flash_args(&std::fs::read_to_string(dir.join("flash_args")).map_err(|e| e.to_string())?);
    if parts.is_empty() {
        return Err("flash_args names no parts".into());
    }
    let mut out = vec![];
    for (off, rel) in parts {
        let f = dir.join(&rel);
        let size = std::fs::metadata(&f).map_err(|_| format!("{rel} is missing: build again"))?.len();
        out.push((off, rel, size));
    }
    let desc: serde_json::Value = std::fs::read(dir.join("project_description.json"))
        .ok()
        .and_then(|b| serde_json::from_slice(&b).ok())
        .unwrap_or_default();
    if let Some(t) = desc["target"].as_str() {
        if t != "esp32s3" {
            return Err(format!("this build is for {t}; the Stack is an ESP32-S3"));
        }
    }
    Ok(Build {
        dir: dir.display().to_string(),
        name: desc["project_name"].as_str().unwrap_or("build").to_string(),
        version: desc["project_version"].as_str().unwrap_or("").to_string(),
        parts: out,
    })
}

pub fn read(b: &Build) -> Result<Vec<Part>, String> {
    b.parts
        .iter()
        .map(|(off, rel, _)| {
            let data = std::fs::read(Path::new(&b.dir).join(rel)).map_err(|e| format!("{rel}: {e}"))?;
            Ok(Part { offset: *off, name: rel.rsplit('/').next().unwrap_or(rel).to_string(), data })
        })
        .collect()
}

#[cfg(test)]
mod tests {
    #[test]
    fn flash_args() {
        let p = super::parse_flash_args("--flash_mode dio --flash_freq 80m --flash_size 4MB\n0x0 bootloader/bootloader.bin\n0x10000 sing.bin\n0x8000 partition_table/partition-table.bin\n");
        assert_eq!(p, vec![(0, "bootloader/bootloader.bin".into()), (0x8000, "partition_table/partition-table.bin".into()), (0x10000, "sing.bin".into())]);
    }
}
