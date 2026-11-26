// Engine and event-mapping checks. Built with ASan + UBSan by `make test`.
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "../app/events.hpp"
#include "../core/sprig.hpp"

#define CHECK(cond)                                                            \
  do {                                                                         \
    if (!(cond)) {                                                             \
      std::fprintf(stderr, "%s:%d: CHECK(%s) failed\n", __FILE__, __LINE__, #cond); \
      std::exit(1);                                                            \
    }                                                                          \
  } while (0)

using sprig::Engine;
using sprig::Mood;

namespace {

void check_frame(const sprig::Frame& f) {
  CHECK(f.strokes == 6);
  CHECK(f.points <= sprig::kMaxPoints);
  for (int s = 0; s < f.strokes; ++s) {
    CHECK(f.count[s] > 0);
    CHECK(f.start[s] + f.count[s] <= f.points);
  }
  for (int i = 0; i < 2 * f.points; ++i) {
    CHECK(std::isfinite(f.xy[i]));
    CHECK(f.xy[i] >= 0 && f.xy[i] <= sprig::kViewBox);
  }
}

void every_mood_stays_in_bounds() {
  // Ragged and hostile frame times: zero, negative, NaN, tiny, huge.
  const float dts[] = {0.016f, 0.033f, 0.25f, 1.0f, 0.0f, -1.0f, NAN, 1e-6f, 60.0f};
  for (int m = 0; m < sprig::kMoodCount; ++m) {
    Engine e{42};
    e.set_mood(static_cast<Mood>(m));
    for (int i = 0; i < 3000; ++i) {
      if (i % 97 == 0) e.set_mood(static_cast<Mood>(m));  // repeated events
      e.step(dts[i % 9]);
      check_frame(e.frame());
    }
  }
}

void same_seed_same_frames() {
  Engine a{7}, b{7};
  for (int i = 0; i < 500; ++i) {
    if (i % 50 == 0) a.set_mood(static_cast<Mood>(i / 50 % sprig::kMoodCount)),
                     b.set_mood(static_cast<Mood>(i / 50 % sprig::kMoodCount));
    a.step(1.0f / 60);
    b.step(1.0f / 60);
  }
  CHECK(std::memcmp(a.frame().xy, b.frame().xy, sizeof a.frame().xy) == 0);
}

void moods_wear_off() {
  const auto run = [](Engine& e, float seconds) {
    for (float t = 0; t < seconds; t += 0.05f) e.step(0.05f);
  };
  Engine e;
  e.set_mood(Mood::happy);
  run(e, 2.0f);
  CHECK(e.mood() == Mood::happy);
  run(e, 1.0f);
  CHECK(e.mood() == Mood::idle);

  e.set_mood(Mood::error);
  run(e, 4.5f);
  CHECK(e.mood() == Mood::idle);

  run(e, 181.0f);
  CHECK(e.mood() == Mood::sleep);

  e.set_mood(Mood::waiting);
  run(e, 300.0f);
  CHECK(e.mood() == Mood::waiting);  // waiting for you does not time out

  e.set_mood(static_cast<Mood>(200));  // out of range: ignored
  CHECK(e.mood() == Mood::waiting);
}

void events_map_to_moods() {
  CHECK(sprig::parse_mood("busy") == Mood::busy);
  CHECK(!sprig::parse_mood("Busy"));
  CHECK(!sprig::parse_mood(""));

  CHECK(sprig::mood_from_hook(R"({"session_id":"x","hook_event_name":"PreToolUse","tool_name":"Bash"})") ==
        Mood::busy);
  CHECK(sprig::mood_from_hook(R"({ "hook_event_name" : "Stop" })") == Mood::happy);
  CHECK(sprig::mood_from_hook(R"({"hook_event_name":"PostToolUseFailure"})") == Mood::error);
  CHECK(sprig::mood_from_hook(R"({"hook_event_name":"Notification"})") == Mood::waiting);
  // The key quoted inside another string is not the real one.
  CHECK(sprig::mood_from_hook(
            R"({"prompt":"say \"hook_event_name\":\"Stop\"","hook_event_name":"UserPromptSubmit"})") ==
        Mood::thinking);
  CHECK(!sprig::mood_from_hook(R"({"hook_event_name":"SomethingNew"})"));
  CHECK(!sprig::mood_from_hook(""));
  CHECK(!sprig::mood_from_hook(R"({"hook_event_name":)"));
  CHECK(!sprig::mood_from_hook(R"({"hook_event_name":"Stop)"));
  CHECK(!sprig::mood_from_hook(R"("hook_event_name)"));
}

}  // namespace

int main() {
  every_mood_stays_in_bounds();
  same_seed_same_frames();
  moods_wear_off();
  events_map_to_moods();
  std::puts("core_test ok");
}
