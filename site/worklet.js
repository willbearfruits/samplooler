// The Samplooler engine, ported from firmware/main/recorder_f32.c for the browser demo.
// One take, one fractional read head, cubic Hermite reads, a 10 ms seam crossfade,
// and the rate slewed toward its target every sample so tilting never zippers.
class Samplooler extends AudioWorkletProcessor {
  constructor() {
    super();
    this.max = Math.floor(sampleRate * 20);
    this.buf = new Float32Array(this.max);
    this.len = 0;          // loop length in samples, 0 = nothing recorded
    this.rec = false;
    this.w = 0;            // record head
    this.pos = 0;          // read head
    this.rate = 0;
    this.target = 0.05;
    this.slew = 1 / (0.05 * sampleRate);   // about 50 ms to follow a jump
    this.xf = Math.floor(0.01 * sampleRate);
    this.peak = 0;
    this.frame = 0;
    this.port.onmessage = (e) => {
      const m = e.data;
      if (m.type === 'rec') {
        if (m.on) { this.rec = true; this.w = 0; }
        else { this.rec = false; this.len = this.w; this.pos = 0; this.env(); }
      } else if (m.type === 'rate') this.target = m.value;
      else if (m.type === 'load') {
        const d = m.data, n = Math.min(d.length, this.max);
        this.buf.set(d.subarray(0, n)); this.len = n; this.pos = 0; this.rec = false; this.env();
      }
    };
  }
  // 64-point loudness outline of the take, for the little screen.
  env() {
    const n = 64, out = new Array(n).fill(0);
    if (this.len) for (let i = 0; i < n; i++) {
      const a = Math.floor(i * this.len / n), b = Math.floor((i + 1) * this.len / n);
      let p = 0; for (let j = a; j < b; j += 8) p = Math.max(p, Math.abs(this.buf[j]));
      out[i] = p;
    }
    this.port.postMessage({ type: 'env', env: out, len: this.len / sampleRate });
  }
  at(i) {
    const n = this.len;
    i %= n; if (i < 0) i += n;
    return this.buf[i];
  }
  // Hermite read at a fractional position, with the loop start faded under the loop end.
  read(p) {
    const i = Math.floor(p), f = p - i;
    const y0 = this.at(i - 1), y1 = this.at(i), y2 = this.at(i + 1), y3 = this.at(i + 2);
    const c1 = 0.5 * (y2 - y0), c2 = y0 - 2.5 * y1 + 2 * y2 - 0.5 * y3, c3 = 0.5 * (y3 - y0) + 1.5 * (y1 - y2);
    let s = ((c3 * f + c2) * f + c1) * f + y1;
    if (p < this.xf && this.len > 4 * this.xf) {
      const g = p / this.xf, q = p + this.len - this.xf;          // tail of the take under the head
      const t = this.at(Math.floor(q));
      s = s * Math.sin(g * Math.PI / 2) + t * Math.cos(g * Math.PI / 2);
    }
    return s;
  }
  process(inputs, outputs) {
    const inp = inputs[0] && inputs[0][0], out = outputs[0];
    const n = out[0].length;
    let pk = 0;
    for (let k = 0; k < n; k++) {
      let y = 0;
      if (this.rec) {
        const x = inp ? inp[k] : 0;
        if (this.w < this.max) this.buf[this.w++] = x;
        pk = Math.max(pk, Math.abs(x));
      } else if (this.len > 8) {
        this.rate += Math.max(-this.slew * 4, Math.min(this.slew * 4, this.target - this.rate));
        y = this.read(this.pos);
        this.pos += this.rate;
        if (this.pos >= this.len) this.pos -= this.len;
        if (this.pos < 0) this.pos += this.len;
        pk = Math.max(pk, Math.abs(y));
      }
      for (let c = 0; c < out.length; c++) out[c][k] = y;
    }
    this.peak = Math.max(pk, this.peak * 0.95);
    if (++this.frame % 8 === 0)
      this.port.postMessage({ type: 'state', pos: this.len ? this.pos / this.len : 0, rate: this.rate,
                              rec: this.rec, fill: this.w / this.max, peak: this.peak });
    return true;
  }
}
registerProcessor('samplooler', Samplooler);
