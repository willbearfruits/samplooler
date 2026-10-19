// A stand-in for the Rust side, so the window runs in a browser: for working on it without a build and
// for its screenshots (tools/shots.sh). ui/app.js loads it only when there is no Tauri. It reads a
// library folder written by `tools/library.py site --local --out <dir>`, served at ../../site/.
//
// ?scene=   one (a Samplooler), update (SING with a newer one in the library), two, download, none
// ?shot=    pick (the install dialog), progress (an install halfway), done, error
(function () {
  const q = new URLSearchParams(location.search);
  const scene = q.get("scene") || "one";
  const shot = q.get("shot");
  const SITE = q.get("site") || "../../site/";
  const handlers = {};
  const emit = (name, payload) => (handlers[name] || []).forEach((f) => f({ payload }));
  const wait = (ms) => new Promise((r) => setTimeout(r, ms));

  const BOARDS = {
    one: [{ key: "midi:Samplooler:Samplooler MIDI 1 24:0", mode: "running", name: "Samplooler", port: "Samplooler:Samplooler MIDI 1 24:0", serial: "SL-727600D0", mac_tail: "72:76:00:D0", version: "0.1.0", proto: 1 }],
    update: [{ key: "midi:SING:SING MIDI 1 24:0", mode: "running", name: "SING", port: "SING:SING MIDI 1 24:0", serial: "SG-727600D0", mac_tail: "72:76:00:D0", version: "0.0.9", proto: 1 }],
    two: [
      { key: "midi:Samplooler:Samplooler MIDI 1 24:0", mode: "running", name: "Samplooler", port: "Samplooler:Samplooler MIDI 1 24:0", serial: "SL-727600D0", mac_tail: "72:76:00:D0", version: "0.1.0", proto: 1 },
      { key: "midi:WRECK:WRECK MIDI 1 28:0", mode: "running", name: "WRECK", port: "WRECK:WRECK MIDI 1 28:0", serial: "WK-7275FBD4", mac_tail: "72:75:FB:D4", version: "0.1.0", proto: 1 },
    ],
    download: [{ key: "dl:/dev/ttyACM0", mode: "download", name: null, port: "/dev/ttyACM0", serial: "90:DA:72:76:00:D0", mac_tail: "72:76:00:D0", version: null, proto: 0 }],
    none: [],
  };
  let boards = BOARDS[scene] || BOARDS.one;

  // the Samplooler's settings as protocol 1 describes them
  const P = (tag, name, group, kind, min, max, value, extra = {}) => ({ tag, name, group, kind, min: String(min), max: String(max), value: String(value), ...extra });
  const SAMPLOOLER = [
    P("MODE", "MODE", "PLAY", "enum", 0, 1, 0, { labels: "TAPE|GRAIN", help: "TAPE plays the take; GRAIN keeps the pitch apart from the speed" }),
    P("PTCH", "PITCH", "PLAY", "num", -24, 24, 0, { unit: "st", flags: "int,signed", text: "= speed", help: "GRAIN only: the pitch in semitones" }),
    P("GRAN", "GRAIN", "PLAY", "num", 20, 200, 80, { unit: "ms", flags: "int", step: "10" }),
    P("SNAP", "SNAP", "PLAY", "enum", 0, 1, 1, { labels: "OFF|ON", help: "Speeds snap to whole ratios near them" }),
    P("TILT", "TILT", "TILT", "enum", 0, 1, 1, { labels: "OFF|ON", help: "Off: the joystick sets the speed" }),
    P("SPMX", "SPEED MAX", "TILT", "num", 0.25, 8, 2, { unit: "x", step: "0.25", text: "2.00x" }),
    P("SPMN", "SPEED MIN", "TILT", "num", 0, 1, 0.25, { unit: "x", step: "0.05", text: "0.25x" }),
    P("MICG", "MIC GAIN", "SOUND", "num", 0, 36, 18, { unit: "dB", flags: "int" }),
    P("VOL ", "VOLUME", "SOUND", "num", 0, 150, 100, { unit: "%", flags: "int", step: "5" }),
    P("INPT", "INPUT", "SOUND", "enum", 0, 1, 0, { labels: "MIC|USB" }),
    P("MONI", "MONITOR", "SOUND", "enum", 0, 1, 0, { labels: "WHILE REC|ALWAYS" }),
    P("SPKR", "SPEAKER", "SOUND", "enum", 0, 1, 1, { labels: "OFF|ON", flags: "sys", help: "Off holds the amp off: the line out only" }),
    P("VOIC", "VOICE", "VOICE", "enum", 0, 7, 0, { labels: "OFF|TUNE|HARMONY|CHOIR|OCTAVES|VOCODER|ROBOT|WHISPER" }),
    P("KEY ", "KEY", "VOICE", "enum", 0, 11, 9, { labels: "C|C#|D|D#|E|F|F#|G|G#|A|A#|B" }),
    P("SCAL", "SCALE", "VOICE", "enum", 0, 5, 1, { labels: "MAJOR|MINOR|DORIAN|PENTA|BLUES|CHROMATIC" }),
    P("LINK", "LINK", "LINK", "enum", 0, 1, 1, { labels: "OFF|ON", flags: "sys", help: "Play in time with the other boards in the room" }),
    P("GRUP", "GROUP", "LINK", "num", 1, 8, 1, { flags: "int,sys" }),
    P("RANG", "RANGE", "LINK", "enum", 0, 2, 1, { labels: "NEAR|ROOM|FAR", flags: "sys" }),
  ];
  const LOG = [
    "I (312) diag: boot: power on",
    "I (318) main: Samplooler 0.1.0, settings from flash",
    "I (402) usb: USB composite up: MIDI + audio 32000 Hz, logs over SysEx, serial SL-727600D0",
    "I (455) imu: QMI8658 at 0x6B, 500 Hz",
    "I (460) oled: SSD1306 128x64",
    "I (1020) flock: link on, group 1, alone",
    "I (10020) diag: alive: cpu 31 %, 0 overruns, 0 underruns",
    "W (14310) audio: input clipped 3 blocks (mic gain 18 dB)",
    "I (20022) main: rec 2.84 s",
  ];

  let lib = null;
  async function library() {
    if (!lib) {
      const r = await fetch(SITE + "library.json");
      lib = await r.json();
    }
    return {
      source: "https://willbearfruits.github.io/samplooler/", web: true, offline: scene === "offline", updated: lib.updated, repo: lib.repo,
      entries: lib.entries.map((e) => ({ ...e, kept: ["samplooler", "sing", "wreck", "mantra"].includes(e.id) })),
    };
  }

  async function install({ board, id, build }) {
    const parts = 3;
    const name = id ? lib.entries.find((e) => e.id === id).name : "build";
    const to = shot === "progress" ? 0.62 : 1;
    for (let i = 0; i <= 10; i++) { emit("job", { stage: "download", text: `Getting ${name}`, done: i * 120000, total: 1200000 }); await wait(40); }
    if (!board.startsWith("dl:")) { emit("job", { stage: "reboot", text: "Asking it to restart into download mode" }); await wait(300); }
    emit("job", { stage: "connect", text: "Connected to 90:DA:72:76:00:D0" });
    await wait(200);
    const names = ["bootloader.bin", "partition-table.bin", (id || "build") + ".bin"];
    for (let p = 0; p < parts; p++) {
      for (let i = 0; i <= 10; i++) {
        if ((p + i / 10) / parts > to) {
          if (shot === "error") { emit("job", { stage: "error", text: "writing failed: the board stopped answering (Timeout while running command)" }); }
          return;
        }
        emit("job", { stage: "write", text: "Writing " + names[p], done: i * 1000, total: 10000, part: p, parts });
        await wait(30);
      }
      emit("job", { stage: "verify", text: "Checking " + names[p], part: p, parts });
      await wait(60);
    }
    if (shot === "error") { emit("job", { stage: "error", text: "the board did not come back in download mode. Unplug it, hold BOOT while you plug it in, and pick it from the list" }); return; }
    emit("job", { stage: "restart", text: "Restarting the board" });
    await wait(200);
    emit("job", { stage: "back", text: `Waiting for ${name} to start` });
    await wait(300);
    emit("job", { stage: "done", text: `${name} is on the board (90:DA:72:76:00:D0)` });
  }

  const commands = {
    library,
    boards: async () => boards,
    settings: async () => ({ source: null, default_source: "https://willbearfruits.github.io/samplooler/", cache: "~/.cache/io.github.willbearfruits.samplooler.library/library", version: "0.1.0" }),
    set_source: async () => {},
    clear_cache: async () => {},
    install: async (a) => { install(a); },
    inspect_build: async ({ dir }) => ({
      dir: dir.replace(/\/?$/, "") + "/firmware/build", name: "mantra", version: "0.2.0",
      parts: [[0, "bootloader/bootloader.bin", 20896], [0x8000, "partition_table/partition-table.bin", 3072], [0x10000, "mantra.bin", 1185456]],
    }),
    line_open: async ({ port }) => {
      setTimeout(() => {
        const b = boards.find((x) => x.port === port);
        if (b?.proto) {
          SAMPLOOLER.forEach((f, i) => emit("board", { port, msg: { kind: "param", fields: { i: String(i), ...f } } }));
          emit("board", { port, msg: { kind: "param", fields: { end: String(SAMPLOOLER.length) } } });
        }
        LOG.forEach((t) => emit("board", { port, msg: { kind: "log", text: t } }));
      }, 120);
    },
    line_close: async () => {},
    line_set: async ({ tag, value }) => {
      const port = boards[0]?.port;
      setTimeout(() => emit("board", { port, msg: { kind: "value", fields: { tag, value } } }), 20);
    },
    line_cc: async () => {},
    line_describe: async () => {},
    line_status: async () => {
      const port = boards[0]?.port;
      emit("board", { port, msg: { kind: "log", text: "I (61234) diag: status: up 61 s, cpu 31 %, heap 182 KB, psram 7.9 MB, last reset: power on, no crash kept" } });
    },
  };

  window.__TAURI__ = {
    core: {
      invoke: (cmd, args) => (commands[cmd] ? commands[cmd](args || {}) : Promise.reject(cmd + ": not in the mock")),
      convertFileSrc: (p) => SITE + p,
    },
    event: { listen: async (name, f) => { (handlers[name] = handlers[name] || []).push(f); return () => {}; } },
    dialog: { open: async () => "/home/you/samplooler/instruments/mantra" },
    opener: { openUrl: async (u) => console.log("open", u) },
  };

  // screenshots: drive the window to the state asked for
  if (shot) {
    window.addEventListener("load", async () => {
      await wait(700);
      const id = q.get("id") || (scene === "update" ? "sing" : "mantra");
      document.querySelector(`[data-install="${id}"]`)?.click();
      if (shot === "pick") return;
      await wait(150);
      document.querySelector('[data-act="go"]')?.click();
    });
  }
})();
