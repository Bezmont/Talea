#pragma once
#include "core/Engine.hpp"
#include "core/Model.hpp"

#include <string>

namespace talea {

constexpr int kStateVersion = 1;

/** Serializes the pattern to a JSON string (core rule 6). Not real-time safe: call from a
UI / save thread, never the audio thread. */
std::string patternToJson(const Pattern& pattern);

/** Restores a pattern from patternToJson() output. Missing or invalid fields keep their
defaults. Returns false (leaving `pattern` reset to defaults) if the text isn't usable. */
bool patternFromJson(const std::string& json, Pattern& pattern);

/** The song (entries, Loop) as JSON text, and back; songFromJson() leaves an empty song for
unusable text and skips invalid entries. Not real-time safe. */
std::string songToJson(const Song& song);
bool songFromJson(const std::string& json, Song& song);

} // namespace talea
