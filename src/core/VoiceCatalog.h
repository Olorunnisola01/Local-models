#pragma once

#include <string>
#include <vector>

namespace tts {

enum class Provider {
    Supertonic,
    Kokoro,
    Piper,
    EdgeTts,
    FishSpeech, // remote Fish Audio S2 Pro (Kaggle GPU via cloudflared tunnel)
};

// A single selectable voice for a provider.
struct VoiceEntry {
    std::string shortName;    // Supertonic: "M1".."F5"; Kokoro: "af_heart" etc;
                               // Piper: the .onnx file stem, e.g. "de_DE-thorsten-high".
    std::string friendlyName; // shown in the UI combo box.
    std::string espeakLang;   // Kokoro/Piper only: espeak-ng voice for phonemization
                               // (e.g. "en-us", "de"). Empty for Supertonic.
};

// Supertonic-3 built-in voice styles (M1-M5 male, F1-F5 female).
const std::vector<VoiceEntry>& supertonicVoices();

// Kokoro-82M voices (54 voices across 9 languages), ported from
// app.py's KOKORO_VOICE_NAMES / KOKORO_LANG_MAP.
const std::vector<VoiceEntry>& kokoroVoices();

// Piper (VITS) voices discovered from models/piper/*.onnx.json.
// Call refreshPiperVoices() when the model directory changes.
void refreshPiperVoices(const std::string& piperModelDir);
const std::vector<VoiceEntry>& piperVoices();

// Curated list of Microsoft Edge neural TTS voices (cloud, online only).
// shortName is the Edge voice name (e.g. "en-US-AvaMultilingualNeural"),
// passed verbatim to the speech.platform.bing.com websocket service.
const std::vector<VoiceEntry>& edgeTtsVoices();

// Fish Audio S2 Pro voice slots (remote Kaggle server required).
// shortName is one of: "random", "slot_1", "slot_2", "slot_3".
const std::vector<VoiceEntry>& fishSpeechVoices();

// Returns the voice list for `provider`.
const std::vector<VoiceEntry>& voicesForProvider(Provider provider);

// True if `provider` supports 2-voice percentage blending (Supertonic, Kokoro).
bool supportsVoiceMixing(Provider provider);

// --- Voice metadata helpers (used by the gender/language filter UI) -----------

// Returns "Male", "Female", or "" parsed from a voice's friendlyName
// (which ends with "..., Male)" / "..., Female)" for Edge/Piper voices).
std::string voiceGender(const VoiceEntry& v);

// Returns the BCP-47 locale (e.g. "en-US") parsed from an Edge voice's
// shortName ("en-US-AvaNeural" -> "en-US"). Empty if it doesn't match.
std::string edgeVoiceLocale(const VoiceEntry& v);

// Sorted, de-duplicated list of all locales present in edgeTtsVoices()
// (e.g. {"af-ZA", "am-ET", "ar-BH", ...}), for the language filter dropdown.
const std::vector<std::string>& edgeTtsLocales();

} // namespace tts
