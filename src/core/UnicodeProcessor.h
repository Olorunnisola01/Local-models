#pragma once

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

namespace tts {

// Converts raw UTF-8 text into the text_ids / text_mask tensors expected by
// Supertonic's duration_predictor / text_encoder ONNX models.
//
// This is a C++ port of supertonic's UnicodeProcessor (core.py), including
// NFKD normalization: most precomposed accented Latin characters (e.g.
// German "a-umlaut") and precomposed Hangul syllables are not directly
// represented in unicode_indexer.json, but their NFKD decomposition (base
// letter + combining diacritic / jamo) is, since the model's vocabulary was
// derived from NFKD-normalized training text. NFKD decomposition data is
// loaded from nfkd_map.json (sparse codepoint -> codepoint-sequence map,
// generated from Python's unicodedata) alongside the indexer. Everything
// else (emoji stripping, symbol normalization, abbreviation expansion,
// punctuation-spacing fixes, whitespace collapsing, trailing period, <lang>
// wrapping, and the unicode -> token lookup) is replicated.
class UnicodeProcessor {
public:
    // indexerJsonPath: path to unicode_indexer.json (flat array of 65536 ints).
    // nfkd_map.json is loaded from the same directory if present; if absent,
    // NFKD decomposition is skipped (codepoints pass through unchanged).
    explicit UnicodeProcessor(const std::string& indexerJsonPath);

    struct Tokenized {
        std::vector<int64_t> textIds; // shape [1, L]
        std::vector<float> textMask;  // shape [1, 1, L], all 1.0f
        int64_t length = 0;           // L
    };

    // Applies the preprocessing pipeline then maps each codepoint through
    // the unicode indexer. `lang` is wrapped as "<lang>...</lang>" (use
    // "na" for the language-agnostic fallback).
    Tokenized process(const std::string& utf8Text, const std::string& lang) const;

private:
    std::vector<int32_t> indexer_; // size 65536, -1 = unsupported codepoint
    std::unordered_map<char32_t, std::u32string> nfkdMap_; // codepoint -> NFKD decomposition

    std::u32string applyNfkd(const std::u32string& input) const;
    std::u32string preprocess(const std::u32string& input, const std::string& lang) const;
};

// Decodes a UTF-8 byte string into a sequence of Unicode codepoints.
std::u32string utf8ToCodepoints(const std::string& utf8);

} // namespace tts
