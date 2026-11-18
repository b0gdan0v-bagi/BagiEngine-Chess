#include "OpeningBook.h"

#include <Chess/Game/MoveNotation.h>
#include <Chess/Game/Position.h>

namespace BECore::Chess {

    namespace {

        constexpr char kColumnSeparator = '\t';
        constexpr char kLineSeparator = '\n';
        constexpr char kWordSeparator = ' ';
        constexpr size_t kColumnCount = 3;
        constexpr StringView kTrimmedAtCut = " ,:";

        StringView Trimmed(StringView text) {
            while (!text.empty() && text.front() == kWordSeparator) {
                text.remove_prefix(1);
            }
            while (!text.empty() && text.back() == kWordSeparator) {
                text.remove_suffix(1);
            }
            return text;
        }

        bool IsContinuationByte(char c) {
            return (static_cast<unsigned char>(c) & 0xC0U) == 0x80U;
        }

        size_t CharCount(StringView text) {
            size_t count = 0;
            for (const char c : text) {
                count += IsContinuationByte(c) ? 0 : 1;
            }
            return count;
        }

        /// The byte offset of the code point number @p chars (the size of @p text when it has no more)
        size_t ByteOffsetOfChar(StringView text, size_t chars) {
            size_t seen = 0;
            for (size_t i = 0; i < text.size(); ++i) {
                if (IsContinuationByte(text[i])) {
                    continue;
                }
                if (seen == chars) {
                    return i;
                }
                ++seen;
            }
            return text.size();
        }

        /// A move number ("12.", "12...") has no move in it; a text such as "0-0" is not one
        StringView WithoutMoveNumber(StringView token) {
            size_t digits = 0;
            while (digits < token.size() && token[digits] >= '0' && token[digits] <= '9') {
                ++digits;
            }
            if (digits == 0 || digits >= token.size() || token[digits] != '.') {
                return token;
            }
            while (digits < token.size() && token[digits] == '.') {
                ++digits;
            }
            return token.substr(digits);
        }

        struct TableRow {
            StringView eco;
            StringView name;
            StringView pgn;
        };

        /// The three columns of a line; nothing when it has fewer (a fourth column is ignored)
        Optional<TableRow> SplitRow(StringView line) {
            const size_t firstTab = line.find(kColumnSeparator);
            if (firstTab == StringView::npos) {
                return std::nullopt;
            }
            const size_t secondTab = line.find(kColumnSeparator, firstTab + 1);
            if (secondTab == StringView::npos) {
                return std::nullopt;
            }
            const size_t thirdTab = line.find(kColumnSeparator, secondTab + 1);
            return TableRow{line.substr(0, firstTab), line.substr(firstTab + 1, secondTab - firstTab - 1),
                            line.substr(secondTab + 1, thirdTab == StringView::npos ? StringView::npos : thirdTab - secondTab - 1)};
        }

        /// The plies of a `pgn` text; the views point into @p pgn
        Vector<StringView> SanTokens(StringView pgn) {
            Vector<StringView> tokens;
            size_t at = 0;
            while (at < pgn.size()) {
                while (at < pgn.size() && (pgn[at] == kWordSeparator || pgn[at] == kColumnSeparator)) {
                    ++at;
                }
                size_t end = at;
                while (end < pgn.size() && pgn[end] != kWordSeparator && pgn[end] != kColumnSeparator) {
                    ++end;
                }
                if (const StringView token = WithoutMoveNumber(pgn.substr(at, end - at)); !token.empty()) {
                    tokens.push_back(token);
                }
                at = end;
            }
            return tokens;
        }

        /// The row before the current one: its plies and the position after each (`positions[i]` is the position after `i` plies), so the next row starts where it differs
        struct Replay {
            Vector<StringView> tokens;
            Vector<Position> positions{Position::Start()};

            /// Keeps the first @p count plies
            void KeepPlies(size_t count) {
                tokens.erase(tokens.begin() + static_cast<ptrdiff_t>(count), tokens.end());
                positions.erase(positions.begin() + static_cast<ptrdiff_t>(count) + 1, positions.end());
            }
        };

        /// Replays @p sans after the plies @p replay keeps; false (and a problem) when one of them is no legal move, the plies before it stay in @p replay
        bool ReplayRow(Replay& replay, const Vector<StringView>& sans, size_t lineNumber, Vector<String>* problems) {
            size_t shared = 0;
            while (shared < sans.size() && shared < replay.tokens.size() && sans[shared] == replay.tokens[shared]) {
                ++shared;
            }
            replay.KeepPlies(shared);
            for (size_t ply = shared; ply < sans.size(); ++ply) {
                const Optional<Move> move = ParseSan(replay.positions.back(), sans[ply]);
                if (!move) {
                    if (problems != nullptr) {
                        problems->push_back(Format("line {}: ply {} '{}' is no legal move", lineNumber, ply + 1, String(sans[ply].data(), sans[ply].size())));
                    }
                    return false;
                }
                Position next = replay.positions.back();
                next.DoMove(*move);
                replay.tokens.push_back(sans[ply]);
                replay.positions.push_back(eastl::move(next));
            }
            return true;
        }

        String CutAtWord(StringView text, size_t maxChars) {
            const size_t limit = ByteOffsetOfChar(text, maxChars);
            StringView head = text.substr(0, limit);
            if (limit < text.size() && text[limit] != kWordSeparator) {
                if (const size_t space = head.rfind(kWordSeparator); space != StringView::npos) {
                    head = head.substr(0, space);
                }
            }
            while (!head.empty() && kTrimmedAtCut.find(head.back()) != StringView::npos) {
                head.remove_suffix(1);
            }
            return String(head.data(), head.size());
        }

        /// The text before the first @p mark, or @p text when there is no mark or nothing before it
        StringView BeforeMark(StringView text, char mark) {
            const size_t at = text.find(mark);
            if (at == StringView::npos) {
                return text;
            }
            const StringView before = Trimmed(text.substr(0, at));
            return before.empty() ? text : before;
        }

    }  // namespace

    OpeningBook OpeningBook::Parse(StringView tsv, Vector<String>* problems) {
        OpeningBook book;
        Replay replay;
        size_t lineNumber = 0;
        size_t at = 0;
        while (at < tsv.size()) {
            size_t end = tsv.find(kLineSeparator, at);
            if (end == StringView::npos) {
                end = tsv.size();
            }
            StringView line = tsv.substr(at, end - at);
            at = end + 1;
            ++lineNumber;
            if (!line.empty() && line.back() == '\r') {
                line.remove_suffix(1);
            }
            if (lineNumber == 1 || line.empty()) {
                continue;
            }

            const Optional<TableRow> row = SplitRow(line);
            if (!row) {
                if (problems != nullptr) {
                    problems->push_back(Format("line {}: fewer than {} columns", lineNumber, kColumnCount));
                }
                continue;
            }

            if (!ReplayRow(replay, SanTokens(row->pgn), lineNumber, problems)) {
                continue;
            }
            if (const ZobristKey key = replay.positions.back().Key(); !book._entries.contains(key)) {
                book._entries.emplace(key, OpeningEntry{String(row->eco.data(), row->eco.size()), String(row->name.data(), row->name.size())});
            }
        }
        return book;
    }

    const OpeningEntry* OpeningBook::Find(ZobristKey key) const {
        const auto it = _entries.find(key);
        return it == _entries.end() ? nullptr : &it->second;
    }

    Optional<OpeningEntry> OpeningOf(const OpeningBook& book, StringView startFen, const Vector<Move>& moves, size_t count) {
        if (book.IsEmpty()) {
            return std::nullopt;
        }
        auto parsed = Position::FromFen(startFen);
        if (!parsed) {
            return std::nullopt;
        }
        Position position = eastl::move(*parsed);
        const OpeningEntry* last = book.Find(position.Key());
        const size_t plies = eastl::min(count, moves.size());
        for (size_t ply = 0; ply < plies; ++ply) {
            position.DoMove(moves[ply]);
            if (const OpeningEntry* entry = book.Find(position.Key())) {
                last = entry;
            }
        }
        if (last == nullptr) {
            return std::nullopt;
        }
        return *last;
    }

    String OpeningDisplayName(StringView name, size_t maxChars) {
        StringView text = Trimmed(name);
        if (CharCount(text) <= maxChars) {
            return String(text.data(), text.size());
        }
        text = BeforeMark(text, ':');
        if (CharCount(text) <= maxChars) {
            return String(text.data(), text.size());
        }
        text = BeforeMark(text, ',');
        if (CharCount(text) <= maxChars) {
            return String(text.data(), text.size());
        }
        return CutAtWord(text, maxChars);
    }

}  // namespace BECore::Chess
