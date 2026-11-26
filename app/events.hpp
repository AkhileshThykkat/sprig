// Turning outside events into moods.
#pragma once

#include <optional>
#include <string_view>
#include <utility>

#include "../core/sprig.hpp"

namespace sprig {

inline std::optional<Mood> parse_mood(std::string_view text) {
  for (int i = 0; i < kMoodCount; ++i)
    if (text == kMoodNames[i]) return static_cast<Mood>(i);
  return std::nullopt;
}

// The string value of a top-level "key" in a JSON object. Not a JSON parser:
// just enough for a hook payload. Keys inside other strings are escaped
// (\"key\") and skipped; values with escapes are not supported (hook event
// names never have them).
inline std::optional<std::string_view> json_string_field(std::string_view json,
                                                         std::string_view key) {
  const auto skip_space = [&](std::size_t i) {
    while (i < json.size() && (json[i] == ' ' || json[i] == '\t' || json[i] == '\n' || json[i] == '\r'))
      ++i;
    return i;
  };
  for (std::size_t pos = json.find(key); pos != std::string_view::npos; pos = json.find(key, pos + 1)) {
    if (pos == 0 || json[pos - 1] != '"' || (pos >= 2 && json[pos - 2] == '\\')) continue;
    std::size_t i = pos + key.size();
    if (i >= json.size() || json[i] != '"') continue;
    i = skip_space(i + 1);
    if (i >= json.size() || json[i] != ':') continue;
    i = skip_space(i + 1);
    if (i >= json.size() || json[i] != '"') return std::nullopt;
    const std::size_t end = json.find('"', i + 1);
    if (end == std::string_view::npos) return std::nullopt;
    return json.substr(i + 1, end - i - 1);
  }
  return std::nullopt;
}

// Claude Code and Codex both send hooks as JSON on stdin, named by
// hook_event_name. Events not listed here leave the mood alone.
inline std::optional<Mood> mood_from_hook(std::string_view json) {
  static constexpr std::pair<std::string_view, Mood> kEvents[] = {
      {"SessionStart", Mood::idle},         {"UserPromptSubmit", Mood::thinking},
      {"PreToolUse", Mood::busy},           {"PostToolUse", Mood::thinking},
      {"PostToolUseFailure", Mood::error},  {"Notification", Mood::waiting},
      {"PermissionRequest", Mood::waiting}, {"SubagentStop", Mood::thinking},
      {"PreCompact", Mood::thinking},       {"Stop", Mood::happy},
      {"SessionEnd", Mood::sleep},
  };
  const auto event = json_string_field(json, "hook_event_name");
  if (!event) return std::nullopt;
  for (const auto& [name, mood] : kEvents)
    if (*event == name) return mood;
  return std::nullopt;
}

}  // namespace sprig
