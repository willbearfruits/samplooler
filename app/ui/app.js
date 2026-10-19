// The window: the library, the boards, installing, an instrument's manual, a board's settings and log,
// your own build, and where the library comes from. The work happens on the Rust side (src-tauri): this
// asks it with invoke() and listens to its events: "job" while installing, "board" from an open board.
(async function () {
  // in a browser (no Tauri): the stand-in in tools/mock.js, for working on the window and its screenshots
  if (!window.__TAURI__) await new Promise((ok) => {
    const s = document.createElement("script");
    s.src = "../tools/mock.js";
    s.onload = ok;
    document.head.appendChild(s);
  });
  const T = window.__TAURI__;
  const invoke = T.core.invoke;
  const listen = T.event.listen;
  const fileUrl = (rel) => T.core.convertFileSrc(rel, "lib");
  const openUrl = (u) => T.opener.openUrl(u).catch(() => {});

  // ---------------------------------------------------------------- state
  const S = {
    lib: null, libError: null,
    boards: [], board: null, boardsSig: "",
    filter: "all",
    job: null,                    // the install in the dialog: {what, steps, ...}
    line: null,                   // the open board: {port, params: Map, order: [], log: [], described: false}
    build: null, buildError: null,
    settings: null,
  };

  const COLOURS = {
    samplooler: "#f2c94c", sing: "#7cc6f2", band: "#f29b50", wreck: "#ef5b5b", ether: "#a990f0", jam: "#f08ec0",
    knock: "#e8dcc0", huff: "#8fe0a8", howl: "#e46fd0", sniff: "#5fd9cf", chirp: "#c8e660", mantra: "#ff9a8a",
  };
  const STATUS = { played: "PLAYED", board: "RUNS ON A BOARD", simulator: "SIMULATOR ONLY" };
  const USES = { mic: "mic", tilt: "tilt", joystick: "joystick", speaker: "speaker", wifi: "Wi-Fi", bluetooth: "Bluetooth" };

  function colour(id) {
    if (COLOURS[id]) return COLOURS[id];
    let h = 0;
    for (const c of String(id)) h = (h * 31 + c.charCodeAt(0)) % 360;
    return `hsl(${h} 70% 70%)`;
  }

  // ---------------------------------------------------------------- little helpers
  const $ = (q, r = document) => r.querySelector(q);
  const $$ = (q, r = document) => [...r.querySelectorAll(q)];
  const esc = (s) => String(s ?? "").replace(/[&<>"']/g, (c) => ({ "&": "&amp;", "<": "&lt;", ">": "&gt;", '"': "&quot;", "'": "&#39;" }[c]));
  const px = (t, s = 2, f = "small", cls = "") => `<span class="px ${cls}" data-f="${f}" data-s="${s}">${esc(t)}</span>`;
  const kb = (n) => (n >= 1 << 20 ? (n / (1 << 20)).toFixed(1) + " MB" : Math.max(1, Math.round(n / 1024)) + " KB");
  const hex = (n) => "0x" + Number(n).toString(16).toUpperCase().padStart(4, "0");

  function toast(text, ms = 3500) {
    const t = document.createElement("div");
    t.className = "toast";
    t.textContent = text;
    document.body.appendChild(t);
    setTimeout(() => t.remove(), ms);
  }
  function err(e) {
    const t = typeof e === "string" ? e : e?.message || JSON.stringify(e);
    return t.charAt(0).toUpperCase() + t.slice(1);
  }

  // "v0.2.0-3-gabc" and "0.2.0" are both 0.2.0; anything without three numbers is not compared
  function ver(s) { const m = /(\d+)\.(\d+)\.(\d+)/.exec(s || ""); return m ? m.slice(1).map(Number) : null; }
  function newer(a, b) {
    const x = ver(a), y = ver(b);
    if (!x || !y) return false;
    for (let i = 0; i < 3; i++) if (x[i] !== y[i]) return x[i] > y[i];
    return false;
  }

  const entries = () => S.lib?.entries || [];
  const entry = (id) => entries().find((e) => e.id === id);
  const norm = (s) => String(s || "").toUpperCase().replace(/[-_]/g, " ");
  const entryFor = (b) => b && b.name ? entries().find((e) => norm(e.name) === norm(b.name)) : null;
  const current = () => S.boards.find((b) => b.key === S.board) || null;

  // what the picked board runs, and whether the library has something newer of it
  function onBoard(e) {
    const b = current();
    return b && b.mode === "running" && entryFor(b)?.id === e.id ? b : null;
  }
  function update(e) {
    const b = onBoard(e);
    return b && newer(e.version, b.version);
  }
  const installable = (e) => !!(e.firmware?.parts?.length || e.manifest);

  // ---------------------------------------------------------------- the boards
  function boardLabel(b) {
    if (b.mode === "download") return "DOWNLOAD MODE";
    return b.name || "BOARD";
  }
  function renderBoards() {
    const el = $("#boardbar");
    if (!S.boards.length) {
      el.innerHTML = `${px("BOARD", 2, "small", "d lbl")}<span class="nob"><span class="live"></span>No board. Plug a Stack in with USB.</span>`;
    } else {
      el.innerHTML = px("BOARD", 2, "small", "d lbl") + S.boards.map((b) => {
        const e = entryFor(b);
        const sub = b.mode === "download" ? (b.serial || b.port) : [b.version, b.serial].filter(Boolean).join(" · ");
        return `<button class="bchip ${b.mode === "download" ? "dl" : ""}" style="--c:${e ? colour(e.id) : "#bbb"}" aria-pressed="${b.key === S.board}" data-board="${esc(b.key)}" title="${esc(b.port)}">
          <span class="live"></span>${px(boardLabel(b), 2)}<span>${esc(sub)}</span></button>`;
      }).join("");
      $$("[data-board]", el).forEach((x) => x.addEventListener("click", () => {
        S.board = x.dataset.board;
        renderBoards();
        rerender();
      }));
    }
    PX.all(el);
  }

  async function pollBoards() {
    try {
      const list = await invoke("boards");
      const sig = JSON.stringify(list);
      if (sig !== S.boardsSig) {
        S.boardsSig = sig;
        S.boards = list;
        if (!list.find((b) => b.key === S.board)) S.board = (list.find((b) => b.mode === "running") || list[0])?.key || null;
        renderBoards();
        rerender(true);
        if (S.job?.picking) renderJob();
      }
    } catch (e) { /* "busy" while installing */ }
  }

  // ---------------------------------------------------------------- views
  const view = () => $("#view");
  let route = "library";

  function nav(which) {
    $$("[data-nav]").forEach((a) => a.toggleAttribute("aria-current", a.dataset.nav === which));
    PX.soon();
  }

  function go() {
    const parts = location.hash.replace(/^#\/?/, "").split("/");
    const [a, b, c] = parts;
    const was = route;
    route = a === "i" ? `i/${b}/${c || ""}` : a || "library";
    if (was === "board" && route !== "board") closeLine();
    nav(a === "i" ? "library" : a || "library");
    window.scrollTo(0, 0);
    draw();
  }

  // a change in the boards redraws what depends on them; `quiet` keeps the board view's open line
  function rerender(quiet) {
    if (route === "library" || route.startsWith("i/")) draw();
    else if (route === "board") viewBoard(quiet);
    else if (route === "build") viewBuild();
  }

  function draw() {
    if (route === "library") return viewLibrary();
    if (route.startsWith("i/")) {
      const [, id, sub] = route.split("/");
      return sub ? viewDoc(id, sub) : viewEntry(id);
    }
    if (route === "board") return viewBoard();
    if (route === "build") return viewBuild();
    if (route === "settings") return viewSettings();
    viewLibrary();
  }

  function put(html) {
    view().innerHTML = html;
    PX.all(view());
  }

  function libProblem() {
    if (S.libError) return `<div class="note bad"><p>The library did not load: ${esc(S.libError)}</p>
      <div class="btns"><button class="btn" data-act="reload">${px("TRY AGAIN")}</button><a class="btn" href="#/settings">${px("SETTINGS")}</a></div></div>`;
    return `<p class="mute">Loading the library…</p>`;
  }

  function tagsFor(e) {
    const t = [`<span class="tag">${px(STATUS[e.status] || e.status)}</span>`];
    if (onBoard(e)) t.unshift(`<span class="tag fill">${px("ON THE BOARD")}</span>`);
    if (update(e)) t.unshift(`<span class="tag up">${px("UPDATE " + e.version)}</span>`);
    return t.join("");
  }

  function installLabel(e) {
    if (!installable(e)) return "NO FIRMWARE YET";
    if (update(e)) return "UPDATE";
    if (onBoard(e)) return "INSTALL AGAIN";
    return "INSTALL";
  }

  function card(e) {
    const first = e.id === "samplooler";
    const uses = (e.uses || []).map((u) => USES[u] || u).join(", ");
    const meta = [uses && "Uses " + uses, e.version, e.kept && S.lib.web ? "kept on this computer" : null].filter(Boolean).join(" · ");
    return `<article class="fw${first ? " first" : ""}" style="--c:${colour(e.id)}" data-id="${esc(e.id)}">
      <a class="sc" href="#/i/${esc(e.id)}"><div class="screen${first ? " big" : ""}"><img src="${fileUrl(e.image)}" alt="${esc(e.name)}'s screen, drawn by its firmware" loading="lazy"></div></a>
      <div class="name"><h3><span class="chip"></span><a href="#/i/${esc(e.id)}">${px(e.name, first ? 5 : 3, "med", "c")}</a></h3><span class="tags">${tagsFor(e)}</span></div>
      <p>${esc(e.summary)}</p>
      <p class="meta">${esc(meta)}</p>
      <div class="btns"><button class="btn solid" data-install="${esc(e.id)}" ${installable(e) ? "" : "disabled"}>${px(installLabel(e))}</button>
        ${e.manual_md ? `<a class="btn" href="#/i/${esc(e.id)}/manual">${px("MANUAL")}</a>` : ""}</div>
    </article>`;
  }

  function viewLibrary() {
    if (!S.lib) return put(libProblem()), wire();
    const want = S.filter;
    const list = entries().filter((e) => want === "all" ? true
      : want === "board" ? e.status !== "simulator"
      : want === "simulator" ? e.status === "simulator"
      : want === "kept" ? e.kept : true);
    const f = (k, t) => `<button data-filter="${k}" aria-pressed="${want === k}">${px(t)}</button>`;
    const b = current();
    const up = b && entryFor(b) && update(entryFor(b)) ? entryFor(b) : null;
    put(`
      ${up ? `<div class="note warn" style="--c:${colour(up.id)};display:flex;gap:18px;align-items:center;justify-content:space-between;flex-wrap:wrap;margin-bottom:22px">
        <p style="margin:0">${esc(up.name)} ${esc(up.version)} is in the library. The board has ${esc(b.version)}.</p>
        <button class="btn solid" data-install="${esc(up.id)}">${px("UPDATE IT")}</button></div>` : ""}
      <p class="lede">Each of these is a firmware for the same Stack. Install one, play it, and put the Samplooler back the same way. The board keeps its settings and saved sounds through all of it.</p>
      ${S.lib.offline ? `<div class="note warn" style="margin-bottom:22px"><p>No connection: this is the list as it was last time. What was downloaded before still installs.</p></div>` : ""}
      <div class="filters">${f("all", "ALL")}${f("board", "ON A BOARD")}${f("simulator", "SIMULATOR ONLY")}${S.lib.web ? f("kept", "ON THIS COMPUTER") : ""}
        <span class="count">${list.length} of ${entries().length}</span></div>
      <div class="gallery">${list.map(card).join("")}</div>`);
    $$("[data-filter]").forEach((b) => b.addEventListener("click", () => { S.filter = b.dataset.filter; viewLibrary(); }));
    wire();
  }

  function viewEntry(id) {
    const e = entry(id);
    if (!e) return put(S.lib ? `<a class="crumb" href="#/">${px("< INSTRUMENTS")}</a><p class="mute">${esc(id)} is not in the library.</p>` : libProblem()), wire();
    const imgs = [e.image, ...(e.more || [])];
    const facts = [
      ["Version", e.version],
      ["By", e.author],
      ["Licence", e.licence],
      ["Uses", (e.uses || []).map((u) => USES[u] || u).join(", ")],
      ["Simulator", e.checks ? `${e.checks} checks pass` : null],
      ["Added", e.added],
    ].filter((x) => x[1]);
    put(`
      <a class="crumb" href="#/">${px("< INSTRUMENTS")}</a>
      <div class="hero" style="--c:${colour(e.id)}">
        <div><div class="screen big"><img id="shot" src="${fileUrl(imgs[0])}" alt="${esc(e.name)}'s screen"></div>
          ${imgs.length > 1 ? `<div class="dots">${imgs.map((s, i) => `<button aria-label="picture ${i + 1}" ${i ? "" : 'aria-current="true"'} data-src="${esc(s)}"></button>`).join("")}</div>` : ""}</div>
        <div class="side">
          <h1 style="margin:0">${px(e.name, 5, "med", "c")}</h1>
          <div class="tags">${tagsFor(e)}</div>
          <p>${esc(e.summary)}</p>
          ${e.hardware_note ? `<div class="note"><p>${esc(e.hardware_note)}</p></div>` : ""}
          ${e.status_note ? `<p class="mute small">${esc(e.status_note)}</p>` : ""}
          <dl class="facts">${facts.map(([k, v]) => `<dt>${esc(k)}</dt><dd>${esc(v)}</dd>`).join("")}</dl>
          <div class="btns"><button class="btn solid" data-install="${esc(e.id)}" ${installable(e) ? "" : "disabled"}>${px(installLabel(e))}</button>
            ${e.manual_md ? `<a class="btn" href="#/i/${esc(e.id)}/manual">${px("MANUAL")}</a>` : ""}
            ${e.source ? `<button class="btn quiet" data-open="${esc(e.source)}">${px("SOURCE")}</button>` : ""}</div>
        </div>
      </div>
      ${e.readme_md ? `<div style="margin-top:44px"><div class="label">${px("ABOUT", 2, "small", "d")}</div><article class="doc" id="readme"><p class="mute">…</p></article></div>` : ""}`);
    $$(".dots button").forEach((b) => b.addEventListener("click", () => {
      $("#shot").src = fileUrl(b.dataset.src);
      $$(".dots button").forEach((x) => x.removeAttribute("aria-current"));
      b.setAttribute("aria-current", "true");
    }));
    wire();
    if (e.readme_md) renderMarkdown(e, e.readme_md, $("#readme"), { skipFirstHeading: true, skipFirstImage: true });
  }

  // ---------------------------------------------------------------- markdown (README, manual)
  const mdCache = new Map();
  async function getText(rel) {
    if (mdCache.has(rel)) return mdCache.get(rel);
    const r = await fetch(fileUrl(rel));
    if (!r.ok) throw new Error(await r.text() || r.status);
    const t = await r.text();
    mdCache.set(rel, t);
    return t;
  }

  const slug = (s) => s.toLowerCase().replace(/[^\w\s-]/g, "").trim().replace(/\s+/g, "-");

  async function renderMarkdown(e, rel, el, opt = {}) {
    let text;
    try { text = await getText(rel); } catch (x) { el.innerHTML = `<p class="mute">Could not load it: ${esc(err(x))}</p>`; return []; }
    // the docs have no HTML of their own; any that turns up is shown as text
    const html = marked.parse(text, { gfm: true, renderer: Object.assign(new marked.Renderer(), { html: ({ text }) => esc(text) }) });
    el.innerHTML = html;
    const dir = rel.includes("/") ? rel.slice(0, rel.lastIndexOf("/") + 1) : "";
    const resolve = (u) => {
      const out = [];
      for (const p of (dir + u).split("/")) { if (p === "..") out.pop(); else if (p !== ".") out.push(p); }
      return out.join("/");
    };
    if (opt.skipFirstHeading) el.querySelector("h1")?.remove();
    $$("img", el).forEach((img, i) => {
      const src = img.getAttribute("src") || "";
      if (opt.skipFirstImage && i === 0) { (img.closest("p") || img).remove(); return; }
      if (!/^[a-z]+:/i.test(src)) img.src = fileUrl(resolve(src));
    });
    const heads = [];
    $$("h1, h2, h3, h4", el).forEach((h) => {
      h.id = slug(h.textContent);
      if (h.tagName === "H2") heads.push({ id: h.id, text: h.textContent });
    });
    $$("a", el).forEach((a) => {
      const href = a.getAttribute("href") || "";
      a.addEventListener("click", (ev) => {
        ev.preventDefault();
        if (href.startsWith("#")) return document.getElementById(href.slice(1))?.scrollIntoView({ behavior: "smooth" });
        if (/^https?:/i.test(href)) return openUrl(href);
        const target = resolve(href.split("#")[0]);
        if (target === e.manual_md) location.hash = `#/i/${e.id}/manual`;
        else if (target === e.readme_md) location.hash = `#/i/${e.id}`;
        else if (S.lib?.repo) openUrl(`${S.lib.repo}/blob/main/${e.id === "samplooler" ? "" : "instruments/"}${target.replace(/^[^/]+\//, e.id === "samplooler" ? "" : e.id + "/")}`);
      });
    });
    return heads;
  }

  async function viewDoc(id, sub) {
    const e = entry(id);
    if (!e) return put(libProblem()), wire();
    const rel = sub === "manual" ? e.manual_md : e.readme_md;
    put(`<a class="crumb" href="#/i/${esc(e.id)}">${px("< " + e.name)}</a>
      <div class="docwrap" style="--c:${colour(e.id)}"><nav class="toc" id="toc"></nav><article class="doc" id="doc"><p class="mute">…</p></article></div>`);
    const heads = await renderMarkdown(e, rel, $("#doc"));
    $("#toc").innerHTML = `<div style="margin:0 0 10px 8px">${px(e.name, 2, "med", "c")}</div>` +
      heads.map((h) => `<a href="#" data-to="${esc(h.id)}">${esc(h.text)}</a>`).join("") +
      `<div class="btns" style="margin:16px 0 0 8px"><button class="btn solid" data-install="${esc(e.id)}" ${installable(e) ? "" : "disabled"}>${px(installLabel(e))}</button></div>`;
    $$("#toc [data-to]").forEach((a) => a.addEventListener("click", (ev) => {
      ev.preventDefault();
      document.getElementById(a.dataset.to)?.scrollIntoView({ behavior: "smooth" });
    }));
    PX.all($("#toc"));
    wire($("#toc"));
  }

  // ---------------------------------------------------------------- the board: its settings and log
  async function openLine(b) {
    if (S.line?.port === b.port) return;
    closeLine();
    S.line = { port: b.port, params: new Map(), order: [], log: [], described: false, opened: Date.now(), error: null };
    try {
      await invoke("line_open", { port: b.port });
    } catch (e) {
      S.line.error = err(e);
    }
    setTimeout(() => { if (route === "board") paintParams(); }, 1600);
  }
  function closeLine() {
    if (!S.line) return;
    S.line = null;
    invoke("line_close").catch(() => {});
  }

  function onBoardMsg({ port, msg }) {
    const L = S.line;
    if (!L || L.port !== port) return;
    if (msg.kind === "log") {
      L.log.push(msg.text);
      if (L.log.length > 400) L.log.splice(0, L.log.length - 400);
      paintLog(msg.text);
    } else if (msg.kind === "param") {
      const f = msg.fields;
      if (f.end !== undefined) { L.described = true; paintParams(); return; }
      if (!f.tag) return;
      if (!L.params.has(f.tag)) L.order.push(f.tag);
      L.params.set(f.tag, f);
    } else if (msg.kind === "value") {
      const f = msg.fields;
      const p = L.params.get(f.tag);
      if (!p) return;
      p.value = f.value;
      if (f.text !== undefined) p.text = f.text;
      paintValue(p);
    }
  }

  function boardHead(b) {
    const e = entryFor(b);
    const c = e ? colour(e.id) : "#bbb";
    const back = entry("samplooler");
    const isSl = e?.id === "samplooler";
    return `<div class="boardhead" style="--c:${c}">
      <div class="who"><div>${px(b.mode === "download" ? "DOWNLOAD MODE" : b.name, 5, "med", "c")}</div>
        <div class="mute small">${esc([b.version && "version " + b.version, b.serial, b.port].filter(Boolean).join(" · "))}</div>
        ${e && update(e) ? `<div class="tags"><span class="tag up">${px("UPDATE " + e.version + " IN THE LIBRARY")}</span></div>` : ""}</div>
      <div class="btns">
        ${e && update(e) ? `<button class="btn solid" data-install="${esc(e.id)}">${px("UPDATE")}</button>` : ""}
        ${back && !isSl ? `<button class="btn ${e && update(e) ? "" : "solid"}" data-install="samplooler" style="--c:${colour("samplooler")}">${px("PUT THE SAMPLOOLER BACK")}</button>` : ""}
        <a class="btn" href="#/">${px("INSTALL ANOTHER")}</a>
        ${e?.manual_md ? `<a class="btn quiet" href="#/i/${esc(e.id)}/manual">${px("MANUAL")}</a>` : ""}
      </div></div>`;
  }

  function viewBoard(quiet) {
    const b = current();
    if (!b) {
      closeLine();
      return put(`<div class="note" style="max-width:640px"><p>No board is plugged in.</p>
        <p>Plug the Stack into this computer with a USB cable that carries data. A board running the Samplooler or anything from this library shows up by itself.</p>
        <p>A board running something else, or nothing yet: hold its BOOT button while you plug it in. It shows up in download mode, ready to be written.</p></div>`), wire();
    }
    if (b.mode === "download") {
      closeLine();
      return put(`${boardHead(b)}<div class="note" style="max-width:700px"><p>This board is waiting to be written (its MAC is ${esc(b.serial || "unknown")}). Pick something from the library and install it. To start what is already on it instead, unplug it and plug it in again.</p></div>`), wire();
    }
    if (quiet && S.line?.port === b.port && $("#params")) return;
    put(`${boardHead(b)}
      <div class="cols" style="--c:${colour(entryFor(b)?.id || "")}">
        <section><div class="label">${px("SETTINGS", 2, "small", "d")}</div><div id="params"><p class="mute">Asking the board…</p></div></section>
        <section><div class="label">${px("LOG", 2, "small", "d")}</div>
          <div class="log" id="log"></div>
          <div class="btns" style="margin-top:10px"><button class="btn" data-act="status">${px("STATUS AND LAST CRASH")}</button><button class="btn quiet" data-act="clearlog">${px("CLEAR")}</button></div>
          <p class="small dim" style="margin-top:10px">The board's own messages, as it runs. Useful when something goes wrong: copy them into an issue.</p></section>
      </div>`);
    wire();
    openLine(b).then(() => { paintParams(); $("#log") && ($("#log").textContent = ""); (S.line?.log || []).forEach(paintLog); });
  }

  function groupsOf(L) {
    const g = new Map();
    for (const tag of L.order) {
      const p = L.params.get(tag);
      if ((p.flags || "").split(",").includes("hide")) continue;
      const k = p.group || "";
      if (!g.has(k)) g.set(k, []);
      g.get(k).push(p);
    }
    return g;
  }

  function paintParams() {
    const el = $("#params");
    const L = S.line;
    if (!el || !L) return;
    if (L.error) { el.innerHTML = `<div class="note bad"><p>Could not open the board: ${esc(L.error)}</p></div>`; return; }
    if (!L.order.length) {
      const b = current();
      const e = entryFor(b);
      if (Date.now() - L.opened < 1500) return;
      el.innerHTML = `<div class="note"><p>${esc(b?.name || "This firmware")}${b?.version ? " " + esc(b.version) : ""} does not share its settings with this app yet. ${e ? `Install the latest ${esc(e.name)} to edit them here.` : ""}</p>
        <p>Its log works either way, and its settings are on its own screen and on MIDI.</p></div>`;
      return;
    }
    const g = groupsOf(L);
    el.innerHTML = [...g].map(([name, ps]) => `<div class="group"><div style="margin:0 0 6px">${px(name || "SETTINGS", 2, "small", "c")}</div>
      ${ps.map(paramRow).join("")}</div>`).join("");
    PX.all(el);
    $$("[data-p]", el).forEach(wireParam);
  }

  const flagsOf = (p) => (p.flags || "").split(",").filter(Boolean);
  const num = (x, d = 0) => (Number.isFinite(+x) ? +x : d);

  // a slider runs 0..1000; log params map it onto min..max as a ratio
  function toSlider(p, v) {
    const lo = num(p.min), hi = num(p.max, 1);
    if (hi <= lo) return 0;
    if (flagsOf(p).includes("log") && lo > 0) return Math.round(1000 * Math.log(v / lo) / Math.log(hi / lo));
    return Math.round(1000 * (v - lo) / (hi - lo));
  }
  function fromSlider(p, s) {
    const lo = num(p.min), hi = num(p.max, 1);
    let v = flagsOf(p).includes("log") && lo > 0 ? lo * Math.pow(hi / lo, s / 1000) : lo + (hi - lo) * s / 1000;
    if (flagsOf(p).includes("int")) v = Math.round(v);
    else {
      const st = num(p.step, 0);
      if (st > 0 && !flagsOf(p).includes("log")) v = Math.round(v / st) * st;
      v = +v.toPrecision(4);
    }
    return Math.min(hi, Math.max(lo, v));
  }
  function guessText(p, v) {
    if (p.kind === "enum") return (p.labels || "").split("|")[Math.round(v - num(p.min))] ?? String(v);
    const n = flagsOf(p).includes("int") ? String(Math.round(v)) : String(+(+v).toPrecision(3));
    return (flagsOf(p).includes("signed") && v > 0 ? "+" : "") + n + (p.unit ? " " + p.unit : "");
  }

  function paramRow(p) {
    const v = num(p.value);
    const help = p.help ? `<div class="help">${esc(p.help)}</div>` : "";
    let ctl;
    if (p.kind === "enum") {
      const labels = (p.labels || "").split("|");
      const lo = num(p.min);
      ctl = labels.length <= 4
        ? `<div class="seg" data-p="${esc(p.tag)}" data-kind="seg">${labels.map((l, i) => `<button aria-pressed="${i + lo === Math.round(v)}" data-v="${i + lo}">${px(l)}</button>`).join("")}</div>`
        : `<select data-p="${esc(p.tag)}" data-kind="select">${labels.map((l, i) => `<option value="${i + lo}" ${i + lo === Math.round(v) ? "selected" : ""}>${esc(l)}</option>`).join("")}</select>`;
    } else if (p.kind === "act") {
      ctl = `<div><button class="btn" data-p="${esc(p.tag)}" data-kind="act">${px("RUN")}</button></div>`;
    } else {
      ctl = `<input type="range" min="0" max="1000" step="1" value="${toSlider(p, v)}" data-p="${esc(p.tag)}" data-kind="range" aria-label="${esc(p.name)}">`;
    }
    const shown = p.kind === "act" || ctl.includes('data-kind="seg"') ? "" : p.text || guessText(p, v);
    return `<div class="param" data-row="${esc(p.tag)}">${px(p.name, 2, "small", "pn")}${ctl}<div class="val" data-val="${esc(p.tag)}" ${shown ? "" : "data-quiet"}>${esc(shown)}</div>${help}</div>`;
  }

  const sendTimers = new Map();
  function send(tag, value) {
    clearTimeout(sendTimers.get(tag));
    sendTimers.set(tag, setTimeout(() => invoke("line_set", { tag, value: String(value) }).catch((e) => toast(err(e))), 35));
  }

  function wireParam(el) {
    const tag = el.dataset.p;
    const p = S.line.params.get(tag);
    const val = $(`[data-val="${CSS.escape(tag)}"]`);
    const kind = el.dataset.kind;
    if (kind === "range") {
      el.addEventListener("input", () => {
        const v = fromSlider(p, +el.value);
        p.value = String(v);
        val.textContent = guessText(p, v);
        send(tag, v);
      });
    } else if (kind === "select") {
      el.addEventListener("change", () => { p.value = el.value; val.textContent = guessText(p, +el.value); send(tag, el.value); });
    } else if (kind === "seg") {
      $$("button", el).forEach((b) => b.addEventListener("click", () => {
        $$("button", el).forEach((x) => x.setAttribute("aria-pressed", x === b));
        p.value = b.dataset.v;
        send(tag, b.dataset.v);
        PX.all(el);
      }));
    } else if (kind === "act") {
      el.addEventListener("click", () => send(tag, 1));
    }
  }

  function paintValue(p) {
    const row = $(`[data-row="${CSS.escape(p.tag)}"]`);
    if (!row) return;
    const v = num(p.value);
    const val = $("[data-val]", row);
    if (val && p.kind !== "act" && !val.hasAttribute("data-quiet")) val.textContent = p.text || guessText(p, v);
    const r = $("input[type=range]", row);
    if (r && document.activeElement !== r) r.value = toSlider(p, v);
    const s = $("select", row);
    if (s && document.activeElement !== s) s.value = String(Math.round(v));
    const seg = $(".seg", row);
    if (seg) { $$("button", seg).forEach((b) => b.setAttribute("aria-pressed", +b.dataset.v === Math.round(v))); PX.all(seg); }
  }

  function paintLog(text) {
    const el = $("#log");
    if (!el) return;
    const line = document.createElement("div");
    line.textContent = text;
    if (/^W \(/.test(text)) line.className = "w";
    if (/^E \(/.test(text)) line.className = "e";
    const atEnd = el.scrollTop + el.clientHeight >= el.scrollHeight - 8;
    el.appendChild(line);
    while (el.childElementCount > 400) el.firstChild.remove();
    if (atEnd) el.scrollTop = el.scrollHeight;
  }

  // ---------------------------------------------------------------- your build
  let lastDir = "";
  try { lastDir = localStorage.getItem("buildDir") || ""; } catch (e) {}

  async function pickBuild(dir) {
    S.buildError = null;
    try {
      S.build = await invoke("inspect_build", { dir });
      lastDir = dir;
      try { localStorage.setItem("buildDir", dir); } catch (e) {}
    } catch (e) {
      S.build = null;
      S.buildError = err(e);
    }
    viewBuild();
  }

  function viewBuild() {
    const B = S.build;
    const b = current();
    put(`<p class="lede">Put a firmware you built yourself on the board: an instrument after a change, or a new one. Pick its folder (the instrument, its <span class="code">firmware/</span> or its <span class="code">build/</span>). The app writes the parts the build's flash_args lists, as <span class="code">tools/flash.sh</span> does, so settings and saved sounds stay.</p>
      <div class="btns" style="margin-bottom:22px"><button class="btn solid" data-act="pickbuild" style="--c:#ece8de">${px("PICK A FOLDER")}</button>
        ${lastDir ? `<button class="btn quiet" data-act="lastbuild">${px("AGAIN:")} <span class="code">${esc(lastDir)}</span></button>` : ""}</div>
      ${S.buildError ? `<div class="note bad"><p>${esc(S.buildError)}</p></div>` : ""}
      ${B ? `<div class="note" style="--c:${colour(B.name)}"><div style="margin-bottom:10px">${px(B.name, 4, "med", "c")}</div>
        <p class="small">${esc(B.version ? "version " + B.version + " · " : "")}<span class="code">${esc(B.dir)}</span></p>
        <table class="parts"><tr><th>At</th><th>File</th><th>Size</th></tr>${B.parts.map(([o, f, s]) => `<tr><td>${hex(o)}</td><td>${esc(f)}</td><td>${kb(s)}</td></tr>`).join("")}</table>
        <div class="btns"><button class="btn solid" data-act="installbuild" ${b ? "" : "disabled"}>${px(b ? "INSTALL ON THE BOARD" : "NO BOARD")}</button>
          <button class="btn quiet" data-act="rebuild">${px("READ IT AGAIN")}</button></div></div>` : ""}`);
    wire();
  }

  // ---------------------------------------------------------------- settings
  async function viewSettings() {
    try { S.settings = await invoke("settings"); } catch (e) { S.settings = {}; }
    const st = S.settings;
    const now = S.lib ? `${S.lib.source}${S.lib.offline ? " (no connection: the copy kept from last time)" : ""}` : S.libError || "not loaded";
    put(`<div class="rows">
      <div>${px("LIBRARY", 2, "small", "m")}<div>
        <p class="mute">Where the list of instruments and their firmwares come from: the project page, another address, or a folder on this computer (one that <span class="code">tools/library.py site</span> wrote).</p>
        ${st.env_source ? `<div class="note warn" style="margin-bottom:12px"><p>SAMPLOOLER_LIBRARY is set, so the app reads <span class="code">${esc(st.env_source)}</span> and the setting below waits.</p></div>` : ""}
        <div class="field"><input type="text" id="src" value="${esc(st.source || "")}" placeholder="${esc(st.default_source || "")}" spellcheck="false"></div>
        <div class="btns"><button class="btn solid" data-act="setsrc" style="--c:#ece8de">${px("USE IT")}</button>
          <button class="btn" data-act="srcfolder">${px("A FOLDER")}</button>
          <button class="btn quiet" data-act="srcdefault">${px("THE PROJECT PAGE")}</button>
          <button class="btn quiet" data-act="reload">${px("LOAD AGAIN")}</button></div>
        <p class="small dim" style="margin-top:12px">Now: ${esc(now)}</p></div></div>
      <div>${px("KEPT FILES", 2, "small", "m")}<div>
        <p class="mute">Firmwares, pictures and manuals downloaded so far, so they work without a connection. Firmwares are checked against their checksums before they are written.</p>
        <p class="code">${esc(st.cache || "")}</p>
        <div class="btns"><button class="btn" data-act="clearcache">${px("CLEAR THEM")}</button></div></div></div>
      <div>${px("ABOUT", 2, "small", "m")}<div>
        <p class="mute">Samplooler Library ${esc(st.version || "")}. Free software under the GPL-3.0. It finds a board by its MIDI port, asks it to restart into download mode, writes the bootloader, the partition table and the program, and restarts it. A TWOFOLD on the same computer is left alone.</p>
        <div class="btns"><button class="btn quiet" data-open="https://github.com/willbearfruits/samplooler/tree/main/app">${px("SOURCE")}</button></div></div></div>
    </div>`);
    wire();
  }

  // ---------------------------------------------------------------- installing
  const STEPS = [
    ["download", "Get the firmware"],
    ["reboot", "Restart the board into download mode"],
    ["write", "Write it and check it"],
    ["back", "Start it"],
  ];

  function openInstall(what) {
    // what: {id} from the library or {build} from YOUR BUILD
    const e = what.id ? entry(what.id) : null;
    S.job = {
      what, e, picking: true, running: false, stage: null, text: "", pct: null, state: {}, error: null, done: false,
      name: e ? e.name : (S.build?.name || "your build").toUpperCase(),
      colour: e ? colour(e.id) : colour(S.build?.name || ""),
      board: S.board,
    };
    renderJob();
    const d = $("#job");
    if (!d.open) d.showModal();
  }

  function renderJob() {
    const J = S.job;
    const d = $("#job");
    if (!J) return;
    d.style.setProperty("--c", J.colour);
    const e = J.e;
    if (J.picking) {
      if (!S.boards.find((b) => b.key === J.board)) J.board = S.board;
      const size = e?.firmware?.parts?.reduce((a, p) => a + (p.size || 0), 0);
      d.innerHTML = `<div class="dlg"><div class="strip"></div>
        <div>${px("INSTALL " + J.name, 3, "med", "c")}</div>
        <p class="mute" style="margin:0">${esc([e?.version, size ? kb(size) : null, e && !e.kept && S.lib?.web ? "downloads first" : null].filter(Boolean).join(" · "))}</p>
        ${S.boards.length ? `<div class="pickb">${S.boards.map((b) => `<label><input type="radio" name="b" value="${esc(b.key)}" ${b.key === J.board ? "checked" : ""}>
          <span>${b.mode === "download" ? `A board in download mode <span class="dim">${esc(b.serial || b.port)}</span>` : `The board running <b>${esc(b.name)}</b> <span class="dim">${esc([b.version, b.serial].filter(Boolean).join(" · "))}</span>`}</span></label>`).join("")}</div>`
          : `<div class="note warn"><p>Plug the board in with USB. If it runs something that is not from this library, hold BOOT while you plug it in.</p></div>`}
        ${e?.status === "simulator" ? `<div class="note warn"><p>${esc(e.name)} has only run in its simulator so far, not on a board. If it misbehaves, put the Samplooler back from here.</p></div>` : ""}
        <p class="small mute" style="margin:0">It takes about half a minute. The board restarts twice. Its settings and saved sounds stay on it.</p>
        <div class="btns"><button class="btn solid" data-act="go" ${S.boards.length ? "" : "disabled"}>${px("INSTALL")}</button><button class="btn quiet" data-act="close">${px("CANCEL")}</button></div></div>`;
      $$("input[name=b]", d).forEach((r) => r.addEventListener("change", () => { J.board = r.value; }));
    } else {
      const order = STEPS.map((s) => s[0]);
      const skipReboot = J.board?.startsWith("dl:");
      const pct = J.pct == null ? "" : Math.round(J.pct * 100) + "%";
      d.innerHTML = `<div class="dlg"><div class="strip"></div>
        <div>${px((J.done ? "INSTALLED " : J.error ? "STOPPED: " : "INSTALLING ") + J.name, 3, "med", J.error ? "" : "c")}</div>
        <ol class="steps">${STEPS.map(([k, t], i) => {
          let cls = J.state[k] || "";
          if (k === "reboot" && skipReboot && J.state[k] !== "bad") cls = J.stage && order.indexOf(J.stage) > 1 ? "ok" : "";
          const label = k === "download" && J.what.build ? "Read your build" : k === "reboot" && skipReboot ? "The board is in download mode already" : t;
          return `<li class="${cls}"><span class="box"></span><span>${esc(label)}</span><span class="pct">${cls === "now" && (k === "download" || k === "write") ? pct : ""}</span></li>`;
        }).join("")}</ol>
        <div class="bar"><div style="width:${J.done ? 100 : Math.round(overall(J) * 100)}%"></div></div>
        <div class="jobtext">${esc(J.error || J.text)}</div>
        ${J.error ? `<div class="note" style="font-size:14px"><p>Nothing on the board breaks from a stopped install: try again, or unplug the board, hold BOOT while you plug it in, and install from download mode.</p></div>` : ""}
        <div class="btns">${J.done ? `<button class="btn solid" data-act="close">${px("DONE")}</button><button class="btn" data-act="toboard">${px("ITS SETTINGS")}</button>${J.e?.manual_md ? `<button class="btn quiet" data-act="tomanual">${px("ITS MANUAL")}</button>` : ""}`
          : J.error ? `<button class="btn solid" data-act="again">${px("TRY AGAIN")}</button><button class="btn quiet" data-act="close">${px("CLOSE")}</button>`
          : `<span class="small dim">Keep the board plugged in.</span>`}</div></div>`;
    }
    PX.all(d);
    wire(d);
  }

  // the whole install as one bar: download 20 %, restart 10 %, writing 60 %, starting 10 %
  function overall(J) {
    const at = { download: 0, reboot: 0.2, connect: 0.27, write: 0.3, verify: 0.3, restart: 0.9, back: 0.92, done: 1 }[J.stage] ?? 0;
    if (J.stage === "download" && J.pct != null) return 0.2 * J.pct;
    if ((J.stage === "write" || J.stage === "verify") && J.pct != null) return 0.3 + 0.6 * J.pct;
    return at;
  }

  function onJob(j) {
    const J = S.job;
    if (!J || !J.running) return;
    const map = { download: "download", reboot: "reboot", connect: "write", write: "write", verify: "write", restart: "back", back: "back" };
    const order = STEPS.map((s) => s[0]);
    if (j.stage === "error") {
      J.error = err(j.text);
      J.running = false;
      const k = map[J.stage] || "download";
      J.state[k] = "bad";
      renderJob();
      pollBoards();
      return;
    }
    if (j.stage === "done") {
      order.forEach((k) => (J.state[k] = "ok"));
      J.done = true;
      J.running = false;
      J.text = j.text;
      J.stage = "done";
      renderJob();
      setTimeout(async () => { await refreshLibrary(false); pollBoards(); }, 300);
      return;
    }
    const k = map[j.stage];
    const idx = order.indexOf(k);
    order.forEach((s, i) => { J.state[s] = i < idx ? "ok" : i === idx ? "now" : ""; });
    J.stage = j.stage;
    J.text = j.text;
    if (j.stage === "download" && j.total) J.pct = j.done / j.total;
    else if (j.stage === "write" && j.total && j.parts) J.pct = (j.part + j.done / j.total) / j.parts;
    else if (j.stage === "verify" && j.parts) J.pct = (j.part + 1) / j.parts;
    else if (j.stage === "write" || j.stage === "download") J.pct = J.pct ?? 0;
    else J.pct = null;
    renderJob();
  }

  async function startJob() {
    const J = S.job;
    if (!J.board) return;
    J.picking = false;
    J.running = true;
    J.error = null;
    J.done = false;
    J.state = {};
    J.stage = "download";
    J.text = "";
    J.pct = null;
    if (route === "board") closeLine();
    renderJob();
    try {
      await invoke("install", { board: J.board, id: J.what.id || null, build: J.what.build || null });
    } catch (e) {
      J.running = false;
      J.error = err(e);
      renderJob();
    }
  }

  // ---------------------------------------------------------------- buttons
  function wire(root = document) {
    $$("[data-install]", root).forEach((b) => b.addEventListener("click", (ev) => { ev.preventDefault(); openInstall({ id: b.dataset.install }); }));
    $$("[data-open]", root).forEach((b) => b.addEventListener("click", () => openUrl(b.dataset.open)));
    $$("[data-act]", root).forEach((b) => b.addEventListener("click", () => act(b.dataset.act, b)));
  }

  async function act(a) {
    const d = $("#job");
    switch (a) {
      case "go": return startJob();
      case "again": S.job.picking = true; return renderJob();
      case "close": d.close(); S.job = null; return rerender();
      case "toboard": d.close(); S.job = null; location.hash = "#/board"; return;
      case "tomanual": { const id = S.job.e.id; d.close(); S.job = null; location.hash = `#/i/${id}/manual`; return; }
      case "reload": return refreshLibrary(true);
      case "status": return invoke("line_status").catch((e) => toast(err(e)));
      case "clearlog": if (S.line) S.line.log = []; $("#log") && ($("#log").textContent = ""); return;
      case "pickbuild": {
        const dir = await T.dialog.open({ directory: true, title: "The instrument's folder, its firmware/ or its build/", defaultPath: lastDir || undefined });
        if (dir) pickBuild(dir);
        return;
      }
      case "lastbuild": case "rebuild": return pickBuild(S.build?.dir || lastDir);
      case "installbuild": return openInstall({ build: S.build.dir });
      case "setsrc": await invoke("set_source", { source: $("#src").value.trim() || null }); await refreshLibrary(true); return viewSettings();
      case "srcfolder": {
        const dir = await T.dialog.open({ directory: true, title: "A folder written by tools/library.py site" });
        if (dir) { await invoke("set_source", { source: dir }); await refreshLibrary(true); viewSettings(); }
        return;
      }
      case "srcdefault": await invoke("set_source", { source: null }); await refreshLibrary(true); return viewSettings();
      case "clearcache": await invoke("clear_cache").catch((e) => toast(err(e))); toast("Cleared"); return refreshLibrary(false);
    }
  }

  $("#job").addEventListener("cancel", (ev) => { if (S.job?.running) ev.preventDefault(); else S.job = null; });

  // ---------------------------------------------------------------- start
  async function refreshLibrary(reload) {
    try {
      S.lib = await invoke("library", { reload });
      S.libError = null;
    } catch (e) {
      S.libError = err(e);
      if (reload) S.lib = null;
    }
    renderBoards();
    if (route !== "board" && route !== "settings") draw();
  }

  await listen("job", (ev) => onJob(ev.payload));
  await listen("board", (ev) => onBoardMsg(ev.payload));
  window.addEventListener("hashchange", go);
  go();
  await refreshLibrary(false);
  await pollBoards();
  setInterval(() => { if (!S.job?.running) pollBoards(); }, 2000);
})();
