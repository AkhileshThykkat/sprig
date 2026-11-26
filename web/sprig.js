// <sprig-mascot mood="busy"></sprig-mascot>
//
// The same C++ engine as the terminal viewer, compiled to WebAssembly
// (sprig.wasm, next to this file). Each element gets its own engine instance;
// all of them share one animation loop, which pauses for elements off-screen.
// Respects prefers-reduced-motion. Attributes: mood, color (any CSS colour;
// defaults to the mood's own). Size it with CSS width/height (96px default).

const MOODS = ["idle", "thinking", "busy", "happy", "error", "waiting", "sleep"];
const wasmUrl = new URL("./sprig.wasm", import.meta.url);
const reducedMotion = matchMedia("(prefers-reduced-motion: reduce)");

let compiled;
const compile = () =>
  (compiled ??= fetch(wasmUrl)
    .then((r) => {
      if (!r.ok) throw new Error(`sprig: ${r.status} loading ${wasmUrl}`);
      return r.arrayBuffer();
    })
    .then((bytes) => WebAssembly.compile(bytes)));

const live = new Set();
let frameId = 0;
let lastTime = 0;

function tick(now) {
  const dt = lastTime ? Math.min((now - lastTime) / 1000, 0.25) : 0;
  lastTime = now;
  for (const el of live) el.tick(dt);
  frameId = live.size ? requestAnimationFrame(tick) : 0;
  if (!frameId) lastTime = 0;
}

class SprigMascot extends HTMLElement {
  static observedAttributes = ["mood", "color"];

  #engine = null;
  #loading = null;
  #canvas;
  #ctx;
  #observer = null;
  #dirty = true;

  constructor() {
    super();
    const root = this.attachShadow({ mode: "open" });
    root.innerHTML =
      "<style>:host{display:inline-block;width:96px;height:96px}" +
      "canvas{display:block;width:100%;height:100%}</style><canvas part=canvas></canvas>";
    this.#canvas = root.querySelector("canvas");
    this.#ctx = this.#canvas.getContext("2d");
  }

  get mood() {
    return this.getAttribute("mood") ?? "idle";
  }
  set mood(value) {
    this.setAttribute("mood", value);
  }

  async connectedCallback() {
    if (!this.hasAttribute("role")) this.setAttribute("role", "img");
    this.#label();
    try {
      await (this.#loading ??= this.#load());
    } catch (err) {
      console.error(err);
      return;
    }
    if (!this.isConnected || this.#observer) return;
    this.#observer = new IntersectionObserver(([entry]) => {
      if (entry.isIntersecting) this.#start();
      else live.delete(this);
    });
    this.#observer.observe(this);
  }

  disconnectedCallback() {
    this.#observer?.disconnect();
    this.#observer = null;
    live.delete(this);
  }

  attributeChangedCallback() {
    this.#label();
    this.#applyMood();
    this.#dirty = true;
  }

  // Called by the shared loop.
  tick(dt) {
    if (reducedMotion.matches) {
      if (this.#dirty) this.#settleAndDraw();
      return;
    }
    this.#engine.sprig_step(dt);
    this.#draw();
  }

  async #load() {
    const instance = await WebAssembly.instantiate(await compile());
    this.#engine = instance.exports;
    this.#engine.sprig_init((Math.random() * 2 ** 32) >>> 0 || 1);
    this.#applyMood();
  }

  #start() {
    live.add(this);
    this.#dirty = true;
    if (!frameId) frameId = requestAnimationFrame(tick);
  }

  #label() {
    if (!this.hasAttribute("aria-label") || this.dataset.sprigLabel) {
      this.dataset.sprigLabel = "1";
      this.setAttribute("aria-label", `sprig mascot, ${this.mood}`);
    }
  }

  #applyMood() {
    const index = MOODS.indexOf(this.mood);
    if (this.#engine && index >= 0) this.#engine.sprig_set_mood(index);
  }

  #settleAndDraw() {
    for (let i = 0; i < 8; i++) this.#engine.sprig_step(0.25);
    this.#draw();
    this.#dirty = false;
  }

  #draw() {
    const ex = this.#engine;
    const canvas = this.#canvas;
    const ctx = this.#ctx;
    const dpr = window.devicePixelRatio || 1;
    const w = Math.max(1, Math.round(canvas.clientWidth * dpr));
    const h = Math.max(1, Math.round(canvas.clientHeight * dpr));
    if (canvas.width !== w || canvas.height !== h) {
      canvas.width = w;
      canvas.height = h;
    }

    // Views are cheap; making them per frame stays correct even if memory grows.
    const mem = ex.memory.buffer;
    const maxStrokes = ex.sprig_max_strokes();
    const xy = new Float32Array(mem, ex.sprig_xy(), 2 * ex.sprig_max_points());
    const start = new Uint16Array(mem, ex.sprig_start(), maxStrokes);
    const count = new Uint16Array(mem, ex.sprig_count(), maxStrokes);
    const closed = new Uint8Array(mem, ex.sprig_closed(), maxStrokes);

    const scale = Math.min(w, h) / 100;
    const ox = (w - 100 * scale) / 2;
    const oy = (h - 100 * scale) / 2;
    ctx.clearRect(0, 0, w, h);
    ctx.lineWidth = 2.6 * scale;
    ctx.lineCap = ctx.lineJoin = "round";
    ctx.strokeStyle =
      this.getAttribute("color") || `#${(ex.sprig_color() >>> 0).toString(16).padStart(6, "0")}`;
    ctx.beginPath();
    for (let s = 0, n = ex.sprig_stroke_count(); s < n; s++) {
      for (let k = 0; k < count[s]; k++) {
        const i = 2 * (start[s] + k);
        const x = ox + xy[i] * scale;
        const y = oy + xy[i + 1] * scale;
        k ? ctx.lineTo(x, y) : ctx.moveTo(x, y);
      }
      if (closed[s]) ctx.closePath();
    }
    ctx.stroke();
  }
}

if (!customElements.get("sprig-mascot")) customElements.define("sprig-mascot", SprigMascot);
export { SprigMascot, MOODS };
