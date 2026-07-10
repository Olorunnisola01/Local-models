#include "TextMarkup.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <regex>
#include <sstream>

namespace tts {
namespace {

std::string escapeXml(const std::string& s) {
    std::string out;
    out.reserve(s.size());
    for (char c : s) {
        switch (c) {
            case '&': out += "&amp;"; break;
            case '<': out += "&lt;"; break;
            case '>': out += "&gt;"; break;
            default: out += c; break;
        }
    }
    return out;
}

std::string rateFromSpeed(float speed) {
    const int pct = static_cast<int>(std::lround((speed - 1.0f) * 100.0f));
    return std::string(pct >= 0 ? "+" : "") + std::to_string(pct) + "%";
}

bool isWordChar(unsigned char c) {
    return std::isalnum(c) != 0 || c == '\'' || c == '-';
}

} // namespace

std::string applyDictionary(const std::string& text, const std::vector<PronunciationEntry>& dictionary) {
    if (dictionary.empty() || text.empty()) {
        return text;
    }

    std::string out;
    out.reserve(text.size());
    size_t i = 0;
    while (i < text.size()) {
        if (!isWordChar(static_cast<unsigned char>(text[i]))) {
            out.push_back(text[i]);
            ++i;
            continue;
        }

        const size_t start = i;
        while (i < text.size() && isWordChar(static_cast<unsigned char>(text[i]))) {
            ++i;
        }
        const std::string word = text.substr(start, i - start);

        bool replaced = false;
        for (const PronunciationEntry& entry : dictionary) {
            if (entry.word.empty() || entry.pronunciation.empty()) {
                continue;
            }
            if (word.size() != entry.word.size()) {
                continue;
            }
            bool match = true;
            for (size_t j = 0; j < word.size(); ++j) {
                if (std::tolower(static_cast<unsigned char>(word[j])) !=
                    std::tolower(static_cast<unsigned char>(entry.word[j]))) {
                    match = false;
                    break;
                }
            }
            if (match) {
                out += entry.pronunciation;
                replaced = true;
                break;
            }
        }
        if (!replaced) {
            out += word;
        }
    }
    return out;
}

std::vector<SpeechPart> splitPauseMarkers(const std::string& text) {
    static const std::regex pauseRe(R"(\[pause(?:\s*=\s*|\s+)(\d+)\s*(?:ms)?\])", std::regex::icase);

    std::vector<SpeechPart> parts;
    std::sregex_iterator it(text.begin(), text.end(), pauseRe);
    const std::sregex_iterator end;

    size_t cursor = 0;
    while (it != end) {
        const std::smatch& match = *it;
        const size_t matchPos = static_cast<size_t>(match.position());
        if (matchPos > cursor) {
            SpeechPart part;
            part.text = text.substr(cursor, matchPos - cursor);
            parts.push_back(std::move(part));
        }
        if (!parts.empty()) {
            parts.back().pauseAfterMs = std::stoi(match[1].str());
        } else {
            SpeechPart gap;
            gap.text.clear();
            gap.pauseAfterMs = std::stoi(match[1].str());
            parts.push_back(std::move(gap));
        }
        cursor = matchPos + match.length();
        ++it;
    }

    if (cursor < text.size()) {
        SpeechPart part;
        part.text = text.substr(cursor);
        parts.push_back(std::move(part));
    }
    if (parts.empty()) {
        parts.push_back({text, 0});
    }

    parts.erase(std::remove_if(parts.begin(), parts.end(),
                               [](const SpeechPart& p) { return p.text.empty() && p.pauseAfterMs == 0; }),
                parts.end());
    if (parts.empty()) {
        parts.push_back({text, 0});
    }
    return parts;
}

std::vector<SpeechPart> prepareSpeechParts(const std::string& text,
                                            const std::vector<PronunciationEntry>& dictionary) {
    const std::string withDict = applyDictionary(text, dictionary);
    return splitPauseMarkers(withDict);
}

std::string stripMarkup(const std::string& text) {
    static const std::regex pauseRe(R"(\[pause(?:\s*=\s*|\s+)(\d+)\s*(?:ms)?\])", std::regex::icase);
    static const std::regex emphRe(R"(\[emph\](.*?)\[/emph\])", std::regex::icase);
    static const std::regex starRe(R"(\*([^*]+)\*)");

    std::string out = std::regex_replace(text, pauseRe, " ");
    out = std::regex_replace(out, emphRe, "$1");
    out = std::regex_replace(out, starRe, "$1");
    return out;
}

std::string convertToEdgeSsmlBody(const std::string& text, float speed) {
    static const std::regex pauseRe(R"(\[pause(?:\s*=\s*|\s+)(\d+)\s*(?:ms)?\])", std::regex::icase);

    std::string cleaned = std::regex_replace(text, pauseRe, " ");
    std::ostringstream body;
    body << "<prosody pitch='+0Hz' rate='" << rateFromSpeed(speed) << "' volume='+0%'>";

    size_t i = 0;
    while (i < cleaned.size()) {
        if (cleaned.compare(i, 7, "[emph]", 7) == 0 || cleaned.compare(i, 7, "[EMPH]", 7) == 0) {
            const size_t close = cleaned.find("[/emph]", i + 7);
            if (close == std::string::npos) {
                body << escapeXml(cleaned.substr(i));
                break;
            }
            const std::string inner = cleaned.substr(i + 7, close - (i + 7));
            body << "<emphasis level='moderate'>" << escapeXml(inner) << "</emphasis>";
            i = close + 7;
            continue;
        }
        if (cleaned[i] == '*') {
            const size_t close = cleaned.find('*', i + 1);
            if (close == std::string::npos) {
                body << escapeXml(std::string(1, cleaned[i]));
                ++i;
                continue;
            }
            const std::string inner = cleaned.substr(i + 1, close - (i + 1));
            body << "<emphasis level='moderate'>" << escapeXml(inner) << "</emphasis>";
            i = close + 1;
            continue;
        }
        const size_t next = cleaned.find_first_of("[*", i);
        if (next == std::string::npos) {
            body << escapeXml(cleaned.substr(i));
            break;
        }
        body << escapeXml(cleaned.substr(i, next - i));
        i = next;
    }

    body << "</prosody>";
    return body.str();
}

} // namespace tts