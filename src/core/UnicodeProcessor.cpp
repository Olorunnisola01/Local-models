#include "UnicodeProcessor.h"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <unordered_map>
#include <unordered_set>

#include <nlohmann/json.hpp>

namespace tts {

std::u32string utf8ToCodepoints(const std::string& utf8) {
    std::u32string out;
    out.reserve(utf8.size());
    size_t i = 0;
    while (i < utf8.size()) {
        unsigned char c = static_cast<unsigned char>(utf8[i]);
        char32_t cp = 0;
        size_t extra = 0;
        if ((c & 0x80) == 0) {
            cp = c;
            extra = 0;
        } else if ((c & 0xE0) == 0xC0) {
            cp = c & 0x1F;
            extra = 1;
        } else if ((c & 0xF0) == 0xE0) {
            cp = c & 0x0F;
            extra = 2;
        } else if ((c & 0xF8) == 0xF0) {
            cp = c & 0x07;
            extra = 3;
        } else {
            // Invalid leading byte; skip it.
            ++i;
            continue;
        }
        if (i + extra >= utf8.size()) {
            break;
        }
        bool valid = true;
        for (size_t k = 1; k <= extra; ++k) {
            unsigned char cc = static_cast<unsigned char>(utf8[i + k]);
            if ((cc & 0xC0) != 0x80) {
                valid = false;
                break;
            }
            cp = (cp << 6) | (cc & 0x3F);
        }
        if (!valid) {
            ++i;
            continue;
        }
        out.push_back(cp);
        i += extra + 1;
    }
    return out;
}

namespace {

// Mirrors supertonic.core._EMOJI_PATTERN
bool isEmoji(char32_t c) {
    return (c >= 0x1F600 && c <= 0x1F64F) || (c >= 0x1F300 && c <= 0x1F5FF) ||
           (c >= 0x1F680 && c <= 0x1F6FF) || (c >= 0x1F700 && c <= 0x1F77F) ||
           (c >= 0x1F780 && c <= 0x1F7FF) || (c >= 0x1F800 && c <= 0x1F8FF) ||
           (c >= 0x1F900 && c <= 0x1F9FF) || (c >= 0x1FA00 && c <= 0x1FA6F) ||
           (c >= 0x1FA70 && c <= 0x1FAFF) || (c >= 0x2600 && c <= 0x26FF) ||
           (c >= 0x2700 && c <= 0x27BF) || (c >= 0x1F1E6 && c <= 0x1F1FF);
}

// Mirrors supertonic.core._SYMBOL_REPLACEMENTS (all single-codepoint -> single-codepoint).
const std::unordered_map<char32_t, char32_t>& symbolReplacements() {
    static const std::unordered_map<char32_t, char32_t> table = {
        {0x2013, U'-'}, {0x2011, U'-'}, {0x2014, U'-'}, {0x00AF, U' '},
        {U'_', U' '},   {0x201C, U'"'}, {0x201D, U'"'}, {0x2018, U'\''},
        {0x2019, U'\''}, {0x00B4, U'\''}, {U'`', U'\''}, {U'[', U' '},
        {U']', U' '},   {U'|', U' '},   {U'/', U' '},   {U'#', U' '},
        {0x2192, U' '}, {0x2190, U' '},
    };
    return table;
}

// Mirrors supertonic.core._SPECIAL_SYMBOLS_PATTERN: [♥☆♡©\\]
bool isSpecialSymbol(char32_t c) {
    return c == 0x2665 || c == 0x2606 || c == 0x2661 || c == 0x00A9 || c == U'\\';
}

// Punctuation characters checked by _ENDING_PUNCTUATION_PATTERN.
bool isEndingPunctuation(char32_t c) {
    static const std::u32string set = U".!?;:,'\")]}…。」】〉》›»";
    return set.find(c) != std::u32string::npos;
}

bool isQuoteChar(char32_t c) {
    return c == U'"' || c == U'\'' || c == U'`';
}

bool isAsciiWhitespace(char32_t c) {
    return c == U' ' || c == U'\t' || c == U'\n' || c == U'\r' || c == U'\f' || c == U'\v';
}

void replaceAsciiSubstring(std::u32string& text, const char* from, const char* to) {
    std::u32string f(from, from + strlen(from));
    std::u32string t(to, to + strlen(to));
    size_t pos = 0;
    while ((pos = text.find(f, pos)) != std::u32string::npos) {
        text.replace(pos, f.size(), t);
        pos += t.size();
    }
}

} // namespace

UnicodeProcessor::UnicodeProcessor(const std::string& indexerJsonPath) {
    std::ifstream f(indexerJsonPath, std::ios::binary);
    if (!f) {
        throw std::runtime_error("Unicode indexer not found: " + indexerJsonPath);
    }
    nlohmann::json j;
    f >> j;
    if (!j.is_array()) {
        throw std::runtime_error("Unicode indexer must be a JSON array: " + indexerJsonPath);
    }
    indexer_.reserve(j.size());
    for (const auto& v : j) {
        indexer_.push_back(v.get<int32_t>());
    }

    // Load the NFKD decomposition map from the same directory, if present.
    std::filesystem::path nfkdPath = std::filesystem::path(indexerJsonPath).parent_path() / "nfkd_map.json";
    std::ifstream nf(nfkdPath, std::ios::binary);
    if (nf) {
        nlohmann::json nj;
        nf >> nj;
        for (auto it = nj.begin(); it != nj.end(); ++it) {
            char32_t cp = static_cast<char32_t>(std::stoul(it.key()));
            std::u32string decomp;
            for (const auto& v : it.value()) {
                decomp.push_back(static_cast<char32_t>(v.get<uint32_t>()));
            }
            nfkdMap_.emplace(cp, std::move(decomp));
        }
    }
}

std::u32string UnicodeProcessor::applyNfkd(const std::u32string& input) const {
    if (nfkdMap_.empty()) {
        return input;
    }
    std::u32string out;
    out.reserve(input.size());
    for (char32_t c : input) {
        auto it = nfkdMap_.find(c);
        if (it != nfkdMap_.end()) {
            out += it->second;
        } else {
            out.push_back(c);
        }
    }
    return out;
}

std::u32string UnicodeProcessor::preprocess(const std::u32string& rawInput, const std::string& lang) const {
    // 1. NFKD normalization: decompose precomposed accented characters (and
    // precomposed Hangul syllables) into base + combining-mark sequences,
    // since the model's vocabulary covers those decomposed forms.
    std::u32string input = applyNfkd(rawInput);

    std::u32string text;
    text.reserve(input.size());

    // 2. Remove emojis.
    for (char32_t c : input) {
        if (!isEmoji(c)) {
            text.push_back(c);
        }
    }

    // 3. Normalize symbols.
    {
        const auto& table = symbolReplacements();
        for (char32_t& c : text) {
            auto it = table.find(c);
            if (it != table.end()) {
                c = it->second;
            }
        }
    }

    // 4. Remove special decorative symbols.
    {
        std::u32string filtered;
        filtered.reserve(text.size());
        for (char32_t c : text) {
            if (!isSpecialSymbol(c)) {
                filtered.push_back(c);
            }
        }
        text = std::move(filtered);
    }

    // 5. Expand abbreviations.
    replaceAsciiSubstring(text, "@", " at ");
    replaceAsciiSubstring(text, "e.g.,", "for example, ");
    replaceAsciiSubstring(text, "i.e.,", "that is, ");

    // 6. Fix punctuation spacing: " ," -> ",", " ." -> ".", etc.
    {
        static const char32_t punct[] = {U',', U'.', U'!', U'?', U';', U':', U'\''};
        std::u32string filtered;
        filtered.reserve(text.size());
        for (size_t i = 0; i < text.size(); ++i) {
            if (text[i] == U' ' && i + 1 < text.size()) {
                char32_t next = text[i + 1];
                bool drop = false;
                for (char32_t p : punct) {
                    if (next == p) {
                        drop = true;
                        break;
                    }
                }
                if (drop) {
                    continue; // skip the space
                }
            }
            filtered.push_back(text[i]);
        }
        text = std::move(filtered);
    }

    // 7. Remove duplicate consecutive quote characters.
    {
        std::u32string filtered;
        filtered.reserve(text.size());
        for (size_t i = 0; i < text.size(); ++i) {
            if (i > 0 && text[i] == text[i - 1] && isQuoteChar(text[i])) {
                continue;
            }
            filtered.push_back(text[i]);
        }
        text = std::move(filtered);
    }

    // 8. Collapse whitespace runs to a single space and strip ends.
    {
        std::u32string filtered;
        filtered.reserve(text.size());
        bool lastWasSpace = false;
        for (char32_t c : text) {
            if (isAsciiWhitespace(c)) {
                if (!lastWasSpace) {
                    filtered.push_back(U' ');
                }
                lastWasSpace = true;
            } else {
                filtered.push_back(c);
                lastWasSpace = false;
            }
        }
        // Strip leading/trailing spaces.
        size_t start = filtered.find_first_not_of(U' ');
        size_t end = filtered.find_last_not_of(U' ');
        if (start == std::u32string::npos) {
            filtered.clear();
        } else {
            filtered = filtered.substr(start, end - start + 1);
        }
        text = std::move(filtered);
    }

    // 9. Add trailing period if the text doesn't end with punctuation/quotes/brackets.
    if (!text.empty() && !isEndingPunctuation(text.back())) {
        text.push_back(U'.');
    }

    // 10. Wrap with language token.
    if (!lang.empty()) {
        std::u32string wrapped = U"<";
        for (char c : lang) wrapped.push_back(static_cast<char32_t>(c));
        wrapped += U">";
        wrapped += text;
        wrapped += U"</";
        for (char c : lang) wrapped.push_back(static_cast<char32_t>(c));
        wrapped += U">";
        text = std::move(wrapped);
    }

    return text;
}

UnicodeProcessor::Tokenized UnicodeProcessor::process(const std::string& utf8Text, const std::string& lang) const {
    std::u32string codepoints = utf8ToCodepoints(utf8Text);
    std::u32string processed = preprocess(codepoints, lang);

    Tokenized result;
    result.length = static_cast<int64_t>(processed.size());
    result.textIds.resize(result.length);
    result.textMask.assign(result.length, 1.0f);

    for (int64_t i = 0; i < result.length; ++i) {
        char32_t cp = processed[static_cast<size_t>(i)];
        int32_t idx = -1;
        if (cp < indexer_.size()) {
            idx = indexer_[cp];
        }
        // Unsupported codepoints map to -1 in the Python indexer and are
        // passed through as-is; clamp to 0 to keep the int64 tensor a
        // valid (non-negative) embedding index for ONNX Runtime.
        if (idx < 0) {
            idx = 0;
        }
        result.textIds[static_cast<size_t>(i)] = idx;
    }

    return result;
}

} // namespace tts
