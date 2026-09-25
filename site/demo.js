// Browser demo: record the mic (or a made-up loop), then tilt the phone or drag the slider.
// Rate curve as on the box: a slow crawl when flat, up to 2x at 45 degrees, square law.
const $ = (s) => document.querySelector(s);
const scr = $('#screen'), g = scr.getContext('2d');
const recBtn = $('#rec'), demoBtn = $('#demo'), tiltIn = $('#tilt'), sensorBtn = $('#sensor');
const note = $('#demo-note');
let ctx, node, mic, env = [], len = 0, st = { pos: 0, rate: 0, rec: false, fill: 0, peak: 0 };
let angle = 0, flip = 1, usingSensor = false;

const CRAWL = 0.05, MAX = 2, DEAD = 3;
function rateFor(a) {
  a = -a * flip;                                   // nose down = positive = forward
  const m = Math.abs(a);
  if (m < DEAD) return CRAWL;
  const t = Math.min(1, (m - DEAD) / (45 - DEAD));
  const r = CRAWL + (MAX - CRAWL) * t * t;
  return a > 0 ? r : -r;
}

async function start() {
  if (ctx) return;
  ctx = new AudioContext();
  await ctx.audioWorklet.addModule('worklet.js');
  node = new AudioWorkletNode(ctx, 'samplooler', { numberOfInputs: 1, outputChannelCount: [2] });
  node.connect(ctx.destination);
  node.port.onmessage = (e) => {
    if (e.data.type === 'env') { env = e.data.env; len = e.data.len; }
    else st = e.data;
  };
  setAngle(angle);
}

async function openMic() {
  if (mic) return true;
  try {
    const s = await navigator.mediaDevices.getUserMedia({ audio: { echoCancellation: false, noiseSuppression: false, autoGainControl: false } });
    mic = ctx.createMediaStreamSource(s);
    mic.connect(node);
    return true;
  } catch (err) {
    note.textContent = 'No microphone here. Use the demo sound instead.';
    return false;
  }
}

async function recDown(e) {
  e.preventDefault();
  await start();
  if (ctx.state !== 'running') await ctx.resume();
  if (!(await openMic())) return;
  recBtn.classList.add('on');
  node.port.postMessage({ type: 'rec', on: true });
}
function recUp() {
  if (!node || !recBtn.classList.contains('on')) return;
  recBtn.classList.remove('on');
  node.port.postMessage({ type: 'rec', on: false });
}
recBtn.addEventListener('pointerdown', recDown);
window.addEventListener('pointerup', recUp);
recBtn.addEventListener('pointercancel', recUp);
recBtn.addEventListener('contextmenu', (e) => e.preventDefault());
window.addEventListener('keydown', (e) => { if (e.code === 'Space' && e.target === document.body && !e.repeat) recDown(e); });
window.addEventListener('keyup', (e) => { if (e.code === 'Space') recUp(); });

// A two-bar loop to play with when there is no mic: a thump, a tick and a plucked line.
demoBtn.addEventListener('click', async () => {
  await start();
  if (ctx.state !== 'running') await ctx.resume();
  const sr = ctx.sampleRate, n = Math.floor(sr * 2.4), d = new Float32Array(n);
  const beat = n / 8, notes = [0, 3, 7, 10, 12, 10, 7, 5];
  for (let b = 0; b < 8; b++) {
    const o = Math.floor(b * beat);
    for (let i = 0; i < beat && o + i < n; i++) {
      const t = i / sr;
      let v = 0;
      if (b % 4 === 0) v += Math.sin(2 * Math.PI * (50 + 90 * Math.exp(-t * 30)) * t) * Math.exp(-t * 9) * 0.8;
      if (b % 2 === 1) v += (Math.random() * 2 - 1) * Math.exp(-t * 70) * 0.25;
      const f = 220 * Math.pow(2, notes[b] / 12);
      v += (Math.sin(2 * Math.PI * f * t) + 0.3 * Math.sin(4 * Math.PI * f * t)) * Math.exp(-t * 5) * 0.3;
      d[o + i] += v;
    }
  }
  node.port.postMessage({ type: 'load', data: d });
  note.textContent = 'Demo loop loaded. Tilt or drag.';
});

function setAngle(a) {
  angle = Math.max(-60, Math.min(60, a));
  if (!usingSensor) tiltIn.value = Math.round(angle);
  const r = rateFor(angle);
  if (node) node.port.postMessage({ type: 'rate', value: r });
  $('#box').style.transform = `rotate(${angle}deg)`;
}
tiltIn.addEventListener('input', () => { usingSensor = false; setAngle(+tiltIn.value); });
tiltIn.addEventListener('dblclick', () => setAngle(0));

// Phones: the screen's top edge is the nose. iOS asks for permission on a tap.
sensorBtn.addEventListener('click', async () => {
  try {
    if (typeof DeviceOrientationEvent !== 'undefined' && DeviceOrientationEvent.requestPermission)
      if ((await DeviceOrientationEvent.requestPermission()) !== 'granted') throw 0;
    window.addEventListener('deviceorientation', (e) => {
      if (e.beta == null) return;
      usingSensor = true;
      setAngle(e.beta);
      tiltIn.value = Math.round(angle);
    });
    sensorBtn.textContent = 'Tilt is on. Hold the phone flat, screen up.';
    sensorBtn.disabled = true;
  } catch { note.textContent = 'No tilt sensor access. Use the slider.'; }
});
$('#flip').addEventListener('click', () => { flip = -flip; setAngle(angle); });
if (!('DeviceOrientationEvent' in window) || !matchMedia('(pointer: coarse)').matches) sensorBtn.hidden = true;

// The screen: a 128x64 two-colour OLED, drawn at 3x with gaps between pixels.
const W = 128, H = 64, K = 3;
scr.width = W * K; scr.height = H * K;
const px = new Uint8Array(W * H);
const dot = (x, y) => { if (x >= 0 && x < W && y >= 0 && y < H) px[y * W + x] = 1; };
const rect = (x, y, w, h) => { for (let j = y; j < y + h; j++) for (let i = x; i < x + w; i++) dot(i, j); };
const FONT = { // 3x5 digits and a few letters, enough for the status line
  '0':'7555 7','1':'2622 7','2':'7174 7','3':'7171 7','4':'5571 1','5':'7471 7','6':'7475 7','7':'7111 1','8':'7575 7','9':'7571 7',
  '.':'0000 2','x':'0525 0','s':'3421 6','-':'0070 0','<':'1242 1','>':'4212 4',' ':'0000 0',
  'R':'6565 5','E':'7464 7','C':'7444 7','L':'4444 7','O':'7555 7','P':'7574 4','A':'2575 5','D':'6555 6','Y':'5522 2','T':'7222 2','I':'7222 7','H':'5575 5','N':'6555 5','G':'7455 7','B':'6565 6'
};
function text(s, x, y, k = 1) {
  for (const ch of s) {
    const f = FONT[ch] || FONT[' '], rows = f.replace(' ', '');
    for (let r = 0; r < 5; r++) { const b = parseInt(rows[r], 16); for (let c = 0; c < 3; c++) if (b & (4 >> c)) rect(x + c * k, y + r * k, k, k); }
    x += 4 * k;
  }
}
function draw() {
  px.fill(0);
  const head = st.rec ? `REC ${Math.round(st.fill * 20)}s` : len ? `LOOP ${len.toFixed(1)}s` : 'READY';
  text(head, 1, 2, 2);
  const lvl = Math.min(1, st.peak * 1.4); for (let i = 0; i < 6; i++) if (lvl * 6 > i) rect(96 + i * 5, 4, 4, 7);
  const mid = 38, half = 13;
  if (st.rec) {
    const fx = Math.floor(st.fill * (W - 1)); rect(fx, mid - half, 1, 2 * half);
    for (let x = 0; x < W; x += 2) dot(x, mid);
  } else if (env.length) {
    for (let x = 0; x < W; x++) { const h = Math.round(env[Math.floor(x * env.length / W)] * half * 1.6); rect(x, mid - Math.min(h, half), 1, 2 * Math.min(h, half) + 1); }
    const hx = Math.floor(st.pos * (W - 1));
    for (let y = mid - half - 2; y <= mid + half + 2; y++) px[y * W + hx] ^= 1;
  } else for (let x = 0; x < W; x += 2) dot(x, mid);
  const r = st.rate, arrows = r < 0 ? '<' : '>';
  text(`${arrows} ${Math.abs(r).toFixed(2)}x`, 1, 57);
  const tx = 88 + Math.round(Math.max(-1, Math.min(1, -angle * flip / 45)) * 36);   // tilt bar right of the rate
  rect(50, 60, 76, 1); rect(88, 57, 1, 7); rect(tx - 1, 57, 3, 7);
  g.fillStyle = '#05070c'; g.fillRect(0, 0, scr.width, scr.height);
  for (let y = 0; y < H; y++) for (let x = 0; x < W; x++) {
    const on = px[y * W + x], top = y < 16;
    g.fillStyle = on ? (top ? '#ffc828' : '#5aaaff') : (top ? '#161208' : '#060a16');
    g.fillRect(x * K, y * K, K - 1, K - 1);
  }
  requestAnimationFrame(draw);
}
draw();
