// sprig: a small mood-driven mascot for coding agents.
//
// This header is the whole engine. It is freestanding C++20: no heap, no
// exceptions, no standard library. The same code builds natively and for
// wasm32 without libc. Every buffer is fixed-size and every write is
// bounds-checked, so there is nothing to leak or overflow.
//
// Use: Engine e{seed}; e.set_mood(Mood::busy); each frame e.step(dt) and draw
// e.frame(): polylines in a 100 x 100 box, y pointing down.
#pragma once

namespace sprig {

using u8 = unsigned char;
using u16 = unsigned short;
using u32 = unsigned int;

enum class Mood : u8 { idle, thinking, busy, happy, error, waiting, sleep };
inline constexpr int kMoodCount = 7;
inline constexpr const char* kMoodNames[kMoodCount] = {"idle",  "thinking", "busy", "happy",
                                                       "error", "waiting",  "sleep"};

constexpr bool valid(Mood m) { return static_cast<int>(m) < kMoodCount; }
constexpr const char* name(Mood m) { return valid(m) ? kMoodNames[static_cast<int>(m)] : "idle"; }

// 0xRRGGBB per mood, shared by every renderer.
constexpr u32 color(Mood m) {
  constexpr u32 kColors[kMoodCount] = {0x4ADE80, 0x60A5FA, 0xFBBF24, 0x34D399,
                                       0xF87171, 0xC084FC, 0x94A3B8};
  return valid(m) ? kColors[static_cast<int>(m)] : kColors[0];
}

inline constexpr int kMaxPoints = 128;
inline constexpr int kMaxStrokes = 8;
inline constexpr float kViewBox = 100.0f;

// One drawn frame: strokes are runs of points in xy.
struct Frame {
  float xy[2 * kMaxPoints] = {};  // x0, y0, x1, y1, ...
  u16 start[kMaxStrokes] = {};    // first point of each stroke
  u16 count[kMaxStrokes] = {};    // points in each stroke
  u8 closed[kMaxStrokes] = {};    // 1: join the last point back to the first
  u16 strokes = 0;
  u16 points = 0;
};

namespace detail {

inline constexpr float kPi = 3.14159265f;
inline constexpr float kTau = 2 * kPi;

constexpr float abs(float v) { return v < 0 ? -v : v; }
constexpr float clamp(float v, float lo, float hi) { return v < lo ? lo : (v > hi ? hi : v); }

// Taylor series after reducing to [-pi/2, pi/2]; error under 1e-5. Callers
// keep phases wrapped, so inputs stay small; anything wild (or NaN) gives 0.
constexpr float sin(float x) {
  if (!(abs(x) < 1e6f)) return 0;
  const float turns = x / kTau;
  x -= static_cast<float>(static_cast<int>(turns + (turns < 0 ? -0.5f : 0.5f))) * kTau;
  if (x > kPi / 2) x = kPi - x;
  else if (x < -kPi / 2) x = -kPi - x;
  const float x2 = x * x;
  return x * (1 - x2 / 6 * (1 - x2 / 20 * (1 - x2 / 42 * (1 - x2 / 72))));
}
constexpr float cos(float x) { return sin(x + kPi / 2); }

constexpr float advance_phase(float phase, float hz, float dt) {
  phase += kTau * hz * dt;  // hz * dt stays well under one turn per substep
  return phase >= kTau ? phase - kTau : phase;
}

static_assert(abs(sin(kPi / 6) - 0.5f) < 1e-5f);
static_assert(abs(cos(kPi) + 1.0f) < 1e-5f);
static_assert(abs(sin(-7.5f) + 0.938f) < 1e-3f);

// Damped spring toward target. Semi-implicit Euler, stable for the small
// substeps Engine::step takes.
struct Spring {
  float value = 0, velocity = 0, target = 0;

  constexpr void step(float dt, float stiffness, float damping) {
    velocity += (stiffness * (target - value) - damping * velocity) * dt;
    value += velocity * dt;
  }
};

struct Rng {  // xorshift32: tiny, deterministic, good enough for blinks
  u32 state = 0x9E3779B9u;

  constexpr float next() {  // [0, 1)
    state ^= state << 13;
    state ^= state >> 17;
    state ^= state << 5;
    return static_cast<float>(state >> 8) / 16777216.0f;
  }
};

// What a mood looks like: spring targets plus rhythm.
struct Pose {
  float eye, smile, mouth_open, gaze_x, gaze_y, squash, leaf;
  float breath_hz, sway_hz, sway, bob_hz, bob;
};

inline constexpr Pose kPoses[kMoodCount] = {
    // eye   smile  open   gaze_x gaze_y squash  leaf   breath sway_hz sway  bob_hz bob
    {1.00f, 0.35f, 0.00f, 0.0f, 0.0f, 0.00f, 0.0f, 0.25f, 0.30f, 0.12f, 0.0f, 0.0f},   // idle
    {0.75f, 0.00f, 0.00f, 0.6f, -0.7f, 0.00f, 0.2f, 0.25f, 0.45f, 0.20f, 0.0f, 0.0f},  // thinking
    {1.00f, 0.15f, 0.00f, 0.0f, 0.0f, -0.04f, 0.3f, 0.50f, 1.60f, 0.30f, 2.2f, 2.0f},  // busy
    {0.50f, 1.00f, 0.35f, 0.0f, 0.0f, 0.00f, 0.6f, 0.30f, 1.20f, 0.35f, 2.4f, 3.5f},   // happy
    {0.90f, -0.80f, 0.00f, 0.0f, 0.5f, 0.12f, -0.9f, 0.25f, 7.00f, 0.04f, 0.0f, 0.0f}, // error
    {1.15f, 0.15f, 0.25f, 0.0f, 0.2f, 0.00f, 0.5f, 0.30f, 0.50f, 0.10f, 0.8f, 1.2f},   // waiting
    {0.00f, 0.10f, 0.15f, 0.0f, 0.3f, 0.08f, -0.4f, 0.12f, 0.10f, 0.06f, 0.0f, 0.0f},  // sleep
};

// Appends points to a Frame, silently dropping anything past its capacity.
class Builder {
 public:
  constexpr explicit Builder(Frame& frame) : f_(frame) { f_.strokes = f_.points = 0; }

  constexpr void begin(bool closed) {
    open_ = f_.strokes < kMaxStrokes;
    if (!open_) return;
    f_.start[f_.strokes] = f_.points;
    f_.count[f_.strokes] = 0;
    f_.closed[f_.strokes] = closed ? 1 : 0;
    ++f_.strokes;
  }

  constexpr void add(float x, float y) {
    if (!open_ || f_.points >= kMaxPoints) return;
    f_.xy[2 * f_.points] = clamp(x, 0, kViewBox);
    f_.xy[2 * f_.points + 1] = clamp(y, 0, kViewBox);
    ++f_.points;
    ++f_.count[f_.strokes - 1];
  }

 private:
  Frame& f_;
  bool open_ = false;
};

}  // namespace detail

class Engine {
 public:
  constexpr explicit Engine(u32 seed = 1) { reset(seed); }

  constexpr void reset(u32 seed) {
    rng_.state = seed ? seed : 0x9E3779B9u;
    for (detail::Spring& s : springs_) s = {};
    breath_ = sway_ = bob_ = 0;
    blink_left_ = dart_in_ = 0;
    blink_in_ = 2 + 3 * rng_.next();
    mood_ = Mood::idle;
    mood_time_ = 0;
    aim(true);
    build();
  }

  // Unknown values are ignored. Setting the current mood again replays its
  // little bounce, so repeated events (one per tool call) stay visible.
  constexpr void set_mood(Mood m) {
    if (!valid(m)) return;
    mood_ = m;
    mood_time_ = 0;
    aim(false);
    spring(K::squash).velocity += 1.5f;
    if (m == Mood::error) spring(K::lean).velocity += 3.0f;
  }

  // Advance by dt seconds. Long gaps are capped and split into small
  // substeps, so a stalled tab or a slow terminal never blows up the springs.
  constexpr void step(float dt) {
    if (!(dt > 0)) return;
    if (dt > 0.25f) dt = 0.25f;
    constexpr float kMaxSubstep = 1.0f / 120;
    const int n = static_cast<int>(dt / kMaxSubstep) + 1;
    const float h = dt / static_cast<float>(n);
    for (int i = 0; i < n; ++i) advance(h);
    build();
  }

  [[nodiscard]] constexpr Mood mood() const { return mood_; }
  [[nodiscard]] constexpr const Frame& frame() const { return frame_; }

 private:
  enum class K : u8 { eye, smile, open, gaze_x, gaze_y, squash, leaf, lean, count };

  constexpr detail::Spring& spring(K k) { return springs_[static_cast<int>(k)]; }
  constexpr const detail::Spring& spring(K k) const { return springs_[static_cast<int>(k)]; }
  constexpr const detail::Pose& pose() const { return detail::kPoses[static_cast<int>(mood_)]; }

  // Point every spring at the current pose; snap jumps straight there.
  constexpr void aim(bool snap) {
    const detail::Pose& p = pose();
    const float targets[] = {p.eye, p.smile, p.mouth_open, p.gaze_x, p.gaze_y, p.squash, p.leaf, 0};
    for (int i = 0; i < static_cast<int>(K::count); ++i) {
      springs_[i].target = targets[i];
      if (snap) springs_[i].value = targets[i];
    }
  }

  constexpr void advance(float h) {
    mood_time_ += h;
    if ((mood_ == Mood::happy && mood_time_ > 2.5f) || (mood_ == Mood::error && mood_time_ > 4.0f))
      set_mood(Mood::idle);
    else if (mood_ == Mood::idle && mood_time_ > 180.0f)
      set_mood(Mood::sleep);
    const detail::Pose& p = pose();

    blink_in_ -= h;
    if (blink_in_ <= 0) {
      blink_left_ = 0.14f;
      blink_in_ = 2 + 4 * rng_.next();
    }
    if (blink_left_ > 0) blink_left_ -= h;
    spring(K::eye).target = (blink_left_ > 0 && mood_ != Mood::sleep) ? 0 : p.eye;

    if (mood_ == Mood::busy) {  // eyes dart around while it works
      dart_in_ -= h;
      if (dart_in_ <= 0) {
        dart_in_ = 0.3f + 0.5f * rng_.next();
        spring(K::gaze_x).target = 0.8f * (2 * rng_.next() - 1);
        spring(K::gaze_y).target = 0.5f * (2 * rng_.next() - 1);
      }
    }

    breath_ = detail::advance_phase(breath_, p.breath_hz, h);
    sway_ = detail::advance_phase(sway_, p.sway_hz, h);
    bob_ = detail::advance_phase(bob_, p.bob_hz, h);

    spring(K::eye).step(h, 500, 45);
    spring(K::smile).step(h, 120, 18);
    spring(K::open).step(h, 120, 18);
    spring(K::gaze_x).step(h, 90, 16);
    spring(K::gaze_y).step(h, 90, 16);
    spring(K::squash).step(h, 220, 9);  // underdamped: it jiggles
    spring(K::leaf).step(h, 60, 6);     // floppy
    spring(K::lean).step(h, 80, 7);
  }

  constexpr void build() {
    using detail::clamp;
    using detail::cos;
    using detail::kPi;
    using detail::kTau;
    using detail::sin;

    const detail::Pose& p = pose();
    detail::Builder b{frame_};

    const float hop = p.bob * detail::abs(sin(bob_));
    const float squash = spring(K::squash).value + 0.015f * sin(breath_);
    const float tilt = 0.10f * spring(K::lean).value;
    const float ct = cos(tilt), st = sin(tilt);
    const float cx = 50, cy = 60 - hop, r = 26;
    const float rx = r * (1 + squash), ry = r * (1 - squash);
    // Body-local (origin at the body's centre) to view coordinates.
    const auto put = [&](float x, float y) { b.add(cx + x * ct - y * st, cy + x * st + y * ct); };

    // Body: a soft wobbling blob.
    b.begin(true);
    for (int i = 0; i < 40; ++i) {
      const float a = kTau * static_cast<float>(i) / 40;
      const float wobble = 1 + 0.012f * sin(3 * a + 2 * sway_);
      put(cos(a) * rx * wobble, sin(a) * ry * wobble);
    }

    // Eyes.
    const float gx = 3.0f * spring(K::gaze_x).value, gy = 2.5f * spring(K::gaze_y).value;
    const float eye_h = 0.35f + 3.85f * clamp(spring(K::eye).value, 0, 1.3f);
    constexpr float kSides[] = {-1, 1};
    for (const float side : kSides) {
      b.begin(true);
      for (int i = 0; i < 12; ++i) {
        const float a = kTau * static_cast<float>(i) / 12;
        put(side * 9 + gx + cos(a) * 3.2f, -5 + gy + sin(a) * eye_h);
      }
    }

    // Mouth: a curve (positive smile bends it down in the middle, y is down),
    // closed into a loop when open.
    const float curve = 4 * spring(K::smile).value;
    const float open = 5 * clamp(spring(K::open).value, 0, 1);
    const float mx = 0.6f * gx, my = 8 + 0.6f * gy;
    b.begin(open > 0.3f);
    for (int i = 0; i <= 8; ++i) {
      const float t = -1 + static_cast<float>(i) / 4;
      put(mx + 5 * t, my + curve * (1 - t * t));
    }
    if (open > 0.3f) {
      for (int i = 7; i >= 1; --i) {
        const float t = -1 + static_cast<float>(i) / 4;
        put(mx + 5 * t, my + (curve + open) * (1 - t * t));
      }
    }

    // Stem: bends progressively with the sway, rooted just inside the body.
    const float leaf = spring(K::leaf).value;
    const float theta = p.sway * sin(sway_) + 0.25f * leaf;
    float sx = 0, sy = 1 - ry;
    b.begin(false);
    put(sx, sy);
    float angle = 0;
    for (int k = 1; k <= 4; ++k) {
      angle = theta * static_cast<float>(k) / 4;
      sx += 3 * sin(angle);
      sy -= 3 * cos(angle);
      put(sx, sy);
    }

    // Leaf at the tip: perks up when happy, droops when something failed.
    const float dir = angle + 0.9f - 0.8f * leaf;
    const float ax = sin(dir), ay = -cos(dir);  // along the leaf
    const float nx = -ay, ny = ax;              // across it
    b.begin(true);
    for (int k = 0; k <= 6; ++k) {
      const float s = static_cast<float>(k) / 6, w = 3.5f * sin(kPi * s);
      put(sx + ax * 10 * s + nx * w, sy + ay * 10 * s + ny * w);
    }
    for (int k = 5; k >= 1; --k) {
      const float s = static_cast<float>(k) / 6, w = 3.5f * sin(kPi * s);
      put(sx + ax * 10 * s - nx * w, sy + ay * 10 * s - ny * w);
    }
  }

  Frame frame_{};
  detail::Spring springs_[static_cast<int>(K::count)] = {};
  detail::Rng rng_{};
  Mood mood_ = Mood::idle;
  float mood_time_ = 0;
  float breath_ = 0, sway_ = 0, bob_ = 0;      // phases, radians, kept in [0, 2pi)
  float blink_in_ = 0, blink_left_ = 0, dart_in_ = 0;  // timers, seconds
};

// The engine runs at compile time too, which doubles as a smoke test.
static_assert(Engine{}.frame().strokes == 6);
static_assert(Engine{}.frame().points <= kMaxPoints);

}  // namespace sprig
