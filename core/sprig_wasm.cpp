// WebAssembly exports for web/sprig.js. One engine per module instance; the
// page instantiates the module once per <sprig-mascot>. No allocation: the JS
// side reads the frame straight out of linear memory.
#include "sprig.hpp"

namespace {
constinit sprig::Engine g_engine{};
}

#define SPRIG_EXPORT(name) extern "C" __attribute__((export_name(#name)))

SPRIG_EXPORT(sprig_init) void sprig_init(unsigned seed) { g_engine.reset(seed); }
SPRIG_EXPORT(sprig_set_mood) void sprig_set_mood(int mood) {
  if (mood >= 0 && mood < sprig::kMoodCount) g_engine.set_mood(static_cast<sprig::Mood>(mood));
}
SPRIG_EXPORT(sprig_step) void sprig_step(float dt) { g_engine.step(dt); }
SPRIG_EXPORT(sprig_mood) int sprig_mood() { return static_cast<int>(g_engine.mood()); }
SPRIG_EXPORT(sprig_color) unsigned sprig_color() { return sprig::color(g_engine.mood()); }

SPRIG_EXPORT(sprig_xy) const float* sprig_xy() { return g_engine.frame().xy; }
SPRIG_EXPORT(sprig_start) const sprig::u16* sprig_start() { return g_engine.frame().start; }
SPRIG_EXPORT(sprig_count) const sprig::u16* sprig_count() { return g_engine.frame().count; }
SPRIG_EXPORT(sprig_closed) const sprig::u8* sprig_closed() { return g_engine.frame().closed; }
SPRIG_EXPORT(sprig_stroke_count) int sprig_stroke_count() { return g_engine.frame().strokes; }
SPRIG_EXPORT(sprig_point_count) int sprig_point_count() { return g_engine.frame().points; }
SPRIG_EXPORT(sprig_max_points) int sprig_max_points() { return sprig::kMaxPoints; }
SPRIG_EXPORT(sprig_max_strokes) int sprig_max_strokes() { return sprig::kMaxStrokes; }
