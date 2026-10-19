// A stand-in for the Rust side, so the window runs in a browser: for working on it without a build and
// for its screenshots (tools/shots.sh). ui/app.js loads it only when there is no Tauri. It reads a
// library folder written by `tools/library.py site --local --out <dir>`, served at ../../site/.
//
// ?scene=   one (a Samplooler), mantra (MANTRA), update (SING with a newer one in the library), two, download, none
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
    mantra: [{ key: "midi:MANTRA:MANTRA MIDI 1 24:0", mode: "running", name: "MANTRA", port: "MANTRA:MANTRA MIDI 1 24:0", serial: "MN-727600D0", mac_tail: "72:76:00:D0", version: "0.2.0", proto: 1 }],
    update: [{ key: "midi:SING:SING MIDI 1 24:0", mode: "running", name: "SING", port: "SING:SING MIDI 1 24:0", serial: "SG-727600D0", mac_tail: "72:76:00:D0", version: "0.0.9", proto: 1 }],
    two: [
      { key: "midi:Samplooler:Samplooler MIDI 1 24:0", mode: "running", name: "Samplooler", port: "Samplooler:Samplooler MIDI 1 24:0", serial: "SL-727600D0", mac_tail: "72:76:00:D0", version: "0.1.0", proto: 1 },
      { key: "midi:WRECK:WRECK MIDI 1 28:0", mode: "running", name: "WRECK", port: "WRECK:WRECK MIDI 1 28:0", serial: "WK-7275FBD4", mac_tail: "72:75:FB:D4", version: "0.1.0", proto: 1 },
    ],
    download: [{ key: "dl:/dev/ttyACM0", mode: "download", name: null, port: "/dev/ttyACM0", serial: "90:DA:72:76:00:D0", mac_tail: "72:76:00:D0", version: null, proto: 0 }],
    none: [],
  };
  let boards = BOARDS[scene] || BOARDS.one;

  // the Samplooler's settings as its firmware describes them (firmware/main/app_line.c): steps, each with the
  // texts the screen shows; a press of - or + moves one along, as a turn of the encoder
  const range = (a, b, d, f) => Array.from({ length: Math.round((b - a) / d) + 1 }, (_, k) => f(a + k * d));
  const VOICES = ["OFF", "CLEAN", "NATURAL TUNE", "HARD TUNE", "CHOIR", "BARBERSHOP", "THIRDS", "OCTAVE DOWN", "ROBOT", "VOCODER", "TALKBOX", "WHISPER", "CHIPMUNK", "MONSTER", "BIG THROAT", "SPACE ECHO", "CATHEDRAL", "SHIMMER", "DUB VOICE"];
  const KEYS = ["C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"];
  const ST = (i, name, group, texts, at, help, extra = {}) => ({ i: String(i), tag: "p" + String(i).padStart(2, "0"), name, group, kind: "step", texts, at, text: texts[at], help, ...extra });
  const ACT = (i, name, group, text, help, extra = {}) => ({ i: String(i), tag: "p" + String(i).padStart(2, "0"), name, group, kind: "act", text, help, ...extra });
  const SAMPLOOLER = [
    ST(1, "Mode", "PLAY", ["TAPE", "GRAIN"], 0, "TAPE: the pitch follows the speed. GRAIN: they are apart."),
    ST(2, "Pitch", "PLAY", ["= speed"], 0, "Semitones, in GRAIN only."),
    ST(6, "Snap", "PLAY", ["OFF", "ON"], 0, "Speeds rounded to octaves and fifths."),
    ST(7, "Grain", "PLAY", range(20, 200, 10, (v) => v + " ms"), 3, "The grain's length, in GRAIN only. Short is smooth, long is choppy."),
    ST(18, "Rec button", "PLAY", ["OFF (BOOT)", "ON"], 1, "Off: the stick's button is ignored. BOOT and MIDI note 60 still record."),
    ST(3, "Tilt", "TILT", ["OFF (joy Y)", "ON"], 1, "Off: it plays forward at 1x, whatever the angle."),
    ST(4, "Speed max", "TILT", range(0.25, 8, 0.25, (v) => v.toFixed(2) + "x"), 7, "The speed at 45 degrees."),
    ST(5, "Speed min", "TILT", range(0, 1, 0.05, (v) => v.toFixed(2) + "x"), 1, "The crawl when flat. 0 stops the tape."),
    ST(0, "Mic gain", "SOUND", range(0, 36, 1, (v) => v + " dB"), 12, "How hard the mic listens."),
    ST(8, "Volume", "SOUND", range(0, 150, 5, (v) => v + "%"), 20, "Above 100% it can clip."),
    ST(9, "Input", "SOUND", ["MIC", "USB"], 0, "USB records what the computer plays into the board."),
    ST(13, "Monitor", "SOUND", ["WHILE REC", "ALWAYS"], 0, "When the input (the voice) is heard."),
    ST(17, "Speaker", "SOUND", ["OFF (line out)", "ON"], 1, "Off: the line out and USB only. The cure for feedback."),
    ST(10, "Voice", "VOICE", VOICES, 0, "A voice on the input, before the recorder.", { wrap: true }),
    ST(11, "Key", "VOICE", KEYS, 0, "The key the voice tunes to. Shared with the boards on the link.", { wrap: true }),
    ST(12, "Scale", "VOICE", ["MAJOR", "MINOR", "DORIAN", "MIXOLYDIAN", "PHRYGIAN", "HARM MINOR"], 0, "The notes of the key."),
    ST(14, "Slot", "SLOTS", ["1 4.2s", "2 12.8s", "3 empty", "4 empty", "5 empty", "6 empty", "7 empty", "8 empty"], 0, "The slot Save and Load use, with its length."),
    ACT(15, "Save", "SLOTS", "-> slot 1", "Writes the loop to the slot.", { confirm: "It writes over what is in the slot." }),
    ACT(16, "Load", "SLOTS", "<- slot 1", "Reads the slot back into the loop."),
    ST(19, "Link", "LINK", ["OFF", "ON"], 1, "The radio to the other instruments."),
    ST(20, "Group", "LINK", range(1, 8, 1, String), 0, "Boards in the same group play together."),
    ST(21, "Range", "LINK", ["NEAR", "ROOM", "FAR"], 1, "The radio's power. Lower is kinder to the supply."),
    ACT(22, "Who is there", "LINK", "2 boards", "Names the boards heard right now, on the board's screen."),
  ];
  // an instrument on the core: ranges and choices, as front.c describes them (MANTRA's, shortened)
  const P = (tag, name, group, kind, min, max, value, extra = {}) => ({ tag, name, group, kind, min: String(min), max: String(max), value: String(value), ...extra });
  const MANTRA = [
    P("hbld", "BLEND", "HANDS", "num", 0, 100, 0, { flags: "int,sys", step: "2", help: "0 THE CHANT, 100 DAISY" }),
    P("hrol", "ROLL", "HANDS", "enum", 0, 2, 0, { labels: "PITCH|OVERTONE|OFF", help: "WHAT ROLLING IT DOES" }),
    P("hrng", "PITCH RANGE", "HANDS", "num", 1, 24, 12, { unit: "st", flags: "int", step: "1", help: "HOW FAR A FULL ROLL GOES" }),
    P("fech", "ECHO", "FX", "num", 0, 100, 30, { unit: "%", flags: "int", step: "5", help: "THE CHANT COMES BACK" }),
    P("fetm", "ECHO TIME", "FX", "enum", 0, 5, 3, { labels: "1/8|1/8 DOT|1/4|1/4 DOT|1/2|1 BAR", help: "IN TIME WITH THE SONG" }),
    P("frev", "REVERB", "FX", "num", 0, 100, 45, { unit: "%", flags: "int", step: "5", help: "HOW LOUD THE HALL IS" }),
    P("frtm", "HALL SIZE", "FX", "num", 0.3, 12, 4, { unit: "s", flags: "log", step: "1.12", help: "HOW LONG IT RINGS" }),
    P("mvoi", "VOICES", "MIX", "num", 0, 100, 85, { unit: "%", flags: "int", step: "5", help: "THE SINGERS' LEVEL" }),
    P("mbow", "BOWL", "MIX", "num", 0, 100, 70, { unit: "%", flags: "int", step: "5", help: "THE SINGING BOWL'S LEVEL" }),
    P("mdrn", "DRONE", "MIX", "num", 0, 100, 0, { unit: "%", flags: "int", step: "5", help: "THE WALL OF AMPS' LEVEL" }),
    P("stmp", "TEMPO", "SONG", "num", 40, 180, 72, { unit: "bpm", flags: "int,sys", step: "1", help: "SHARED WITH THE FLOCK" }),
    P("skey", "KEY", "SONG", "enum", 0, 11, 7, { labels: KEYS.join("|"), flags: "sys", help: "THE KEY THEY SING IN" }),
    P("nlnk", "LINK", "LINK", "enum", 0, 1, 1, { labels: "OFF|ON", flags: "nocc,sys", help: "SYNC WITH BOARDS NEARBY" }),
    P("ngrp", "GROUP", "LINK", "num", 1, 8, 1, { flags: "int,nocc,sys", step: "1", help: "SAME GROUP: THEY SYNC" }),
    P("nwho", "WHO IS THERE", "LINK", "act", 0, 0, 0, { flags: "nocc", help: "THE BOARDS LINKED NOW" }),
    P("lplv", "LOOP LEVEL", "LOOP", "num", 0, 100, 80, { unit: "%", flags: "int,sys", step: "5", help: "HOW LOUD THE LOOP PLAYS" }),
    P("lpcl", "CLEAR LOOP", "LOOP", "act", 0, 0, 0, { flags: "nocc,sys", help: "PRESS TO ERASE IT" }),
    P("vol ", "VOLUME", "SYSTEM", "num", 0, 150, 100, { unit: "%", flags: "int,sys", step: "5", help: "OVER 100%: LIMITER WORKS" }),
    P("spkr", "SPEAKER", "SYSTEM", "enum", 0, 1, 1, { labels: "OFF|ON", flags: "nocc,sys", help: "OFF: THE LINE OUT ONLY" }),
    P("mic ", "MIC GAIN", "SYSTEM", "num", 0, 36, 12, { unit: "dB", flags: "int,nocc,sys", step: "3", help: "HOW HARD THE MIC LISTENS" }),
    P("mch ", "MIDI CH", "SYSTEM", "num", 1, 16, 1, { flags: "int,nocc,sys", step: "1", help: "CHANNEL FOR NOTES AND CC" }),
  ];
  const LOG = (b) => [
    "I (312) diag: boot: power on",
    `I (318) main: ${b.name} ${b.version}, settings from flash`,
    `I (402) usb: USB composite up: MIDI + audio 32000 Hz, logs over SysEx, serial ${b.serial}`,
    "I (455) imu: QMI8658 at 0x6B, 500 Hz",
    "I (460) oled: SSD1306 128x64",
    "I (1020) link: on, group 1, alone",
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
        const set = b?.name === "Samplooler" ? SAMPLOOLER : MANTRA;
        if (b?.proto) {
          set.forEach((f, i) => { const { texts, at, wrap, ...d } = f; emit("board", { port, msg: { kind: "param", fields: { i: String(i), ...d } } }); });
          emit("board", { port, msg: { kind: "param", fields: { end: String(set.length) } } });
        }
        if (b) LOG(b).forEach((t) => emit("board", { port, msg: { kind: "log", text: t } }));
      }, 120);
    },
    line_close: async () => {},
    line_set: async ({ tag, value }) => {
      const port = boards[0]?.port;
      const b = boards.find((x) => x.port === port);
      let fields = { tag, value };
      if (b?.name === "Samplooler") {
        const st = SAMPLOOLER.find((x) => x.tag === tag);
        if (!st) return;
        if (st.kind === "step") {
          const n = st.texts.length;
          st.at = st.wrap ? (st.at + Math.sign(+value) + n) % n : Math.min(n - 1, Math.max(0, st.at + Math.sign(+value)));
          st.text = st.texts[st.at];
        }
        fields = { tag, text: st.text };
      }
      setTimeout(() => emit("board", { port, msg: { kind: "value", fields } }), 20);
    },
    line_ping: async () => {},
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
