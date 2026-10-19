// Pixel lettering from the firmware's fonts (fonts.js, window.PXFONTS): every .px element's text is drawn
// on a canvas in square pixels, in the element's colour. data-f picks the face (small, med, big), data-s
// the size of one pixel in CSS pixels. The text stays in the page for screen readers.
(function () {
  const F = window.PXFONTS;

  function glyph(font, ch) {
    return font.glyphs[ch] || font.glyphs[ch.toUpperCase()] || null;
  }
  function measure(text, f) {
    const font = F[f];
    let w = 0;
    for (const ch of text) {
      const g = ch === " " ? null : glyph(font, ch);
      w += (g ? g[0].length : font.space) + font.spacing;
    }
    return Math.max(0, w - font.spacing);
  }
  function draw(ctx, str, f, cell, gap) {
    const font = F[f];
    let x = 0;
    for (const ch of str) {
      const g = ch === " " ? null : glyph(font, ch);
      if (!g) { x += font.space + font.spacing; continue; }
      for (let r = 0; r < g.length; r++)
        for (let c = 0; c < g[r].length; c++)
          if (g[r][c] === "#") ctx.fillRect((x + c) * cell, r * cell, cell - gap, cell - gap);
      x += g[0].length + font.spacing;
    }
  }
  function letter(el) {
    const str = (el.dataset.t ?? el.textContent.trim()).toUpperCase();
    el.dataset.t = str;
    const f = F[el.dataset.f] ? el.dataset.f : "med";
    const s = +(el.dataset.s || 3);
    const dpr = Math.max(1, Math.round(window.devicePixelRatio || 1));
    const font = F[f];
    const w = measure(str, f), h = font.height;
    const colour = getComputedStyle(el).color;
    const key = [str, f, s, dpr, colour].join("|");
    if (el.dataset.k === key) return;                 // already drawn like this
    el.dataset.k = key;
    const cv = document.createElement("canvas");
    const cell = s * dpr, gap = s >= 3 ? Math.max(1, Math.round(cell * 0.16)) : 0;
    cv.width = Math.max(1, w * cell); cv.height = h * cell;
    cv.style.width = w * s + "px"; cv.style.height = h * s + "px";
    cv.setAttribute("aria-hidden", "true");
    const ctx = cv.getContext("2d");
    ctx.fillStyle = colour;
    draw(ctx, str, f, cell, gap);
    const sr = document.createElement("span");
    sr.textContent = str;
    sr.style.cssText = "position:absolute;width:1px;height:1px;overflow:hidden;clip:rect(0 0 0 0)";
    el.replaceChildren(cv, sr);
  }
  function all(root = document) { root.querySelectorAll(".px").forEach(letter); }
  // re-letter what changed colour (a pressed button, a hovered link)
  let pending = false;
  function soon() {
    if (pending) return;
    pending = true;
    requestAnimationFrame(() => { pending = false; all(); });
  }
  for (const ev of ["mouseover", "mouseout", "focusin", "focusout"]) document.addEventListener(ev, soon, true);
  window.PX = { all, letter, soon };
})();
