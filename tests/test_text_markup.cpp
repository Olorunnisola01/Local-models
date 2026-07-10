// Unit smoke tests for TextMarkup preprocessing (no Qt).

#include <cstdio>
#include <string>

#include "../src/core/TextMarkup.h"

int main() {
    const std::string input = "Hello [pause=200ms] world. [emph]Important[/emph]";
    const auto parts = tts::prepareSpeechParts(input, {});
    if (parts.size() < 2) {
        std::fprintf(stderr, "FAIL: expected >=2 speech parts, got %zu\n", parts.size());
        return 1;
    }
    if (parts[0].pauseAfterMs < 200) {
        std::fprintf(stderr, "FAIL: expected pause >=200ms, got %d\n", parts[0].pauseAfterMs);
        return 1;
    }
    const std::string stripped = tts::stripMarkup(input);
    if (stripped.find('[') != std::string::npos) {
        std::fprintf(stderr, "FAIL: stripMarkup left bracket tags in: %s\n", stripped.c_str());
        return 1;
    }
    std::printf("PASS: prepareSpeechParts + stripMarkup\n");
    return 0;
}