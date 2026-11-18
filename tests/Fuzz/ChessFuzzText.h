#pragma once

/// What the text targets (FuzzUciCommand, FuzzUciOutput) read a line with, written here a second time so that they never call the code under test to learn what it should say
namespace BECore::Chess::Fuzzing::Text {

    using Words = Vector<StringView>;

    inline StringView ViewOf(const uint8_t* data, size_t size) {
        return StringView(reinterpret_cast<const char*>(data), size);
    }

    inline StringView ViewOf(const String& text) {
        return StringView(text.data(), text.size());
    }

    /// The ends of a line that a reader may drop: spaces, and the `\r` or `\n` of the line break
    [[nodiscard]] inline bool IsLineEdge(char symbol) {
        return symbol == ' ' || symbol == '\r' || symbol == '\n';
    }

    [[nodiscard]] inline StringView WithoutEdges(StringView line) {
        while (!line.empty() && IsLineEdge(line.front())) {
            line.remove_prefix(1);
        }
        while (!line.empty() && IsLineEdge(line.back())) {
            line.remove_suffix(1);
        }
        return line;
    }

    /// The non-empty runs of @p line between characters of @p separators
    [[nodiscard]] inline Words WordsOf(StringView line, StringView separators) {
        Words words;
        size_t begin = 0;
        for (size_t index = 0; index <= line.size(); ++index) {
            if (index < line.size() && separators.find(line[index]) == StringView::npos) {
                continue;
            }
            if (index > begin) {
                words.push_back(line.substr(begin, index - begin));
            }
            begin = index + 1;
        }
        return words;
    }

    /// A whole decimal number as UCI writes it: an optional '-', then digits only, and the value fits an int
    [[nodiscard]] inline Optional<int> ReadInt(StringView text) {
        constexpr int64_t kMagnitudeLimit = int64_t{std::numeric_limits<int>::max()} + 1;
        const bool negative = !text.empty() && text.front() == '-';
        const StringView digits = negative ? text.substr(1) : text;
        if (digits.empty()) {
            return std::nullopt;
        }
        int64_t magnitude = 0;
        for (const char symbol : digits) {
            if (symbol < '0' || symbol > '9') {
                return std::nullopt;
            }
            magnitude = magnitude * 10 + (symbol - '0');
            if (magnitude > kMagnitudeLimit) {
                return std::nullopt;
            }
        }
        if (!negative && magnitude == kMagnitudeLimit) {
            return std::nullopt;
        }
        return static_cast<int>(negative ? -magnitude : magnitude);
    }

}  // namespace BECore::Chess::Fuzzing::Text
