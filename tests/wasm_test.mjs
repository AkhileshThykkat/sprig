// Checks the shipped web/sprig.wasm the way web/sprig.js uses it.
import assert from "node:assert/strict";
import { readFile } from "node:fs/promises";

const bytes = await readFile(new URL("../web/sprig.wasm", import.meta.url));
const mod = await WebAssembly.compile(bytes);
assert.equal(WebAssembly.Module.imports(mod).length, 0, "the module must need nothing from its host");

const ex = (await WebAssembly.instantiate(mod)).exports;
ex.sprig_init(123);
const max = ex.sprig_max_points();
const maxStrokes = ex.sprig_max_strokes();

for (let mood = 0; mood < 7; mood++) {
  ex.sprig_set_mood(mood);
  for (let i = 0; i < 400; i++) {
    ex.sprig_step(i % 50 === 0 ? 5 : 1 / 60);
    const mem = ex.memory.buffer;
    const xy = new Float32Array(mem, ex.sprig_xy(), 2 * max);
    const start = new Uint16Array(mem, ex.sprig_start(), maxStrokes);
    const count = new Uint16Array(mem, ex.sprig_count(), maxStrokes);
    const strokes = ex.sprig_stroke_count();
    const points = ex.sprig_point_count();
    assert.equal(strokes, 6);
    for (let s = 0; s < strokes; s++) assert.ok(start[s] + count[s] <= points);
    for (let k = 0; k < 2 * points; k++) assert.ok(xy[k] >= 0 && xy[k] <= 100, `point ${k} = ${xy[k]}`);
  }
}
ex.sprig_set_mood(99); // ignored
assert.ok(ex.sprig_mood() >= 0 && ex.sprig_mood() < 7);
assert.equal(ex.sprig_color() >>> 0 <= 0xffffff, true);
console.log("wasm_test ok");
