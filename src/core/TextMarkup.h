#pragma once

#include <string>
#include <vector>

namespace tts {

struct PronunciationEntry {
    std::string word;
    std::string pronunciation;
};

// One speakable unit produced after dictionary + pause-marker preprocessing.
struct SpeechPart {
    std::string text;
    int pauseAfterMs = 0;
};

// Replaces whole words (case-insensitive) using the pronunciation dictionary.
std::string applyDictionary(const std::string& text, const std::vector<PronunciationEntry>& dictionary);

// Splits text on [pause=300ms] / [pause 300ms] markers into speakable parts.
std::vector<SpeechPart> splitPauseMarkers(const std::string& text);

// Applies dictionary then pause splitting.
std::vector<SpeechPart> prepareSpeechParts(const std::string& text,
                                            const std::vector<PronunciationEntry>& dictionary);

// Strips pause markers and converts [emph]..[/emph] to SSML emphasis spans.
// Plain text segments are XML-escaped. For non-Edge providers, use stripMarkup().
std::string convertToEdgeSsmlBody(const std::string& text, float speed);

// Removes markup tags, leaving speakable plain text for offline engines.
std::string stripMarkup(const std::string& text);

} // namespace tts