#include <Chess/Engine/UciProtocol.h>
#include <tests/Fuzz/ChessFuzzTargets.h>
#include <tests/Fuzz/ChessFuzzText.h>

namespace BECore::Chess::Fuzzing {

    namespace {

        using Text::ReadInt;
        using Text::ViewOf;
        using Text::WithoutEdges;
        using Text::Words;
        using UciProtocol::BestMove;
        using UciProtocol::Info;
        using UciProtocol::LineCollector;

        /// The lines of one input that are read; the rest is left alone so that a large input stays cheap
        constexpr size_t kMostLines = 256;

        constexpr char kLineBreak = '\n';
        constexpr StringView kWordSeparator = " ";

        /// The collector is asked for one line, for a few, and for the most an analysis has
        constexpr int kLineCounts[] = {1, 3, kMaxAnalysisLines};

        using Lines = Vector<StringView>;

        Lines SplitLines(StringView text) {
            Lines lines;
            size_t begin = 0;
            while (begin <= text.size() && lines.size() < kMostLines) {
                size_t end = text.find(kLineBreak, begin);
                if (end == StringView::npos) {
                    end = text.size();
                }
                lines.push_back(text.substr(begin, end - begin));
                begin = end + 1;
            }
            return lines;
        }

        /// ParseBestMove written from its header: the first word is `bestmove` and the move is the word after it, empty when there is none
        Optional<BestMove> ExpectedBestMove(const Words& words) {
            if (words.empty() || words.front() != "bestmove") {
                return std::nullopt;
            }
            BestMove expected;
            if (words.size() > 1) {
                expected.move = String(words[1]);
            }
            return expected;
        }

        /// ParseInfo written from its header: words that name no field are skipped, `string` and `pv` end the line, `depth` and `multipv` take a positive number, `score` takes a kind
        /// and, for the kinds we know, a number (a mate in 0 is no score)
        Optional<Info> ExpectedInfo(const Words& words) {
            if (words.empty() || words.front() != "info") {
                return std::nullopt;
            }
            Info info;
            size_t index = 1;
            while (index < words.size()) {
                const StringView word = words[index++];
                if (word == "string") {
                    break;
                }
                if (word == "pv") {
                    if (index < words.size()) {
                        info.firstPvMove = String(words[index]);
                    }
                    break;
                }
                if (word == "depth" || word == "multipv") {
                    const Optional<int> value = index < words.size() ? ReadInt(words[index]) : std::nullopt;
                    if (value && *value > 0) {
                        (word == "depth" ? info.depth : info.multiPv) = *value;
                    }
                } else if (word == "score") {
                    info.score = std::nullopt;
                    if (index < words.size()) {
                        const StringView kind = words[index++];
                        const Optional<int> value = index < words.size() ? ReadInt(words[index]) : std::nullopt;
                        if (kind == "cp" && value) {
                            info.score = AnalysisScore{AnalysisScoreKind::Centipawns, *value};
                            ++index;
                        } else if (kind == "mate" && value) {
                            if (*value != 0) {
                                info.score = AnalysisScore{AnalysisScoreKind::Mate, *value};
                            }
                            ++index;
                        }
                    }
                } else if (word == "lowerbound") {
                    info.lowerBound = true;
                } else if (word == "upperbound") {
                    info.upperBound = true;
                }
            }
            return info;
        }

        bool SameInfo(const Info& a, const Info& b) {
            return a.depth == b.depth && a.multiPv == b.multiPv && a.score == b.score && a.lowerBound == b.lowerBound && a.upperBound == b.upperBound && a.firstPvMove == b.firstPvMove;
        }

        bool SameInfos(const Vector<Info>& a, const Vector<Info>& b) {
            if (a.size() != b.size()) {
                return false;
            }
            for (size_t index = 0; index < a.size(); ++index) {
                if (!SameInfo(a[index], b[index])) {
                    return false;
                }
            }
            return true;
        }

        /// What LineCollector says it keeps: per `multipv` index the latest exact score, a bound only when no exact one came, the indices in order
        Vector<Info> ExpectedKept(const Lines& lines, int lineCount) {
            Vector<Optional<Info>> exact(static_cast<size_t>(lineCount));
            Vector<Optional<Info>> bound(static_cast<size_t>(lineCount));
            for (const StringView line : lines) {
                const Optional<Info> info = ExpectedInfo(Text::WordsOf(WithoutEdges(line), kWordSeparator));
                if (!info || !info->score || info->multiPv < 1 || info->multiPv > lineCount) {
                    continue;
                }
                const size_t slot = static_cast<size_t>(info->multiPv) - 1;
                ((info->lowerBound || info->upperBound) ? bound : exact)[slot] = info;
            }
            Vector<Info> kept;
            for (size_t slot = 0; slot < exact.size(); ++slot) {
                if (exact[slot]) {
                    kept.push_back(*exact[slot]);
                } else if (bound[slot]) {
                    kept.push_back(*bound[slot]);
                }
            }
            return kept;
        }

        void CheckLine(StringView line) {
            const StringView trimmed = UciProtocol::TrimLine(line);
            // An empty result owns no bytes, so only a non-empty one has a place in the line
            CHESS_FUZZ_CHECK(trimmed.empty() || (trimmed.data() >= line.data() && trimmed.data() + trimmed.size() <= line.data() + line.size()));
            CHESS_FUZZ_CHECK(trimmed.empty() || (!Text::IsLineEdge(trimmed.front()) && !Text::IsLineEdge(trimmed.back())));
            CHESS_FUZZ_CHECK(trimmed == WithoutEdges(line));
            CHESS_FUZZ_CHECK(UciProtocol::TrimLine(trimmed) == trimmed);

            const Words words = Text::WordsOf(trimmed, kWordSeparator);
            const Optional<Info> info = UciProtocol::ParseInfo(line);
            const Optional<BestMove> bestMove = UciProtocol::ParseBestMove(line);
            CHESS_FUZZ_CHECK(!(info && bestMove));

            const Optional<Info> expectedInfo = ExpectedInfo(words);
            CHESS_FUZZ_CHECK(info.has_value() == expectedInfo.has_value());
            if (info) {
                CHESS_FUZZ_CHECK(SameInfo(*info, *expectedInfo));
                CHESS_FUZZ_CHECK(info->firstPvMove.find(' ') == String::npos);
                if (words.size() > 1 && words[1] == "string") {
                    CHESS_FUZZ_CHECK(!info->score && info->depth == 0 && info->firstPvMove.empty());
                }
            }

            const Optional<BestMove> expectedBestMove = ExpectedBestMove(words);
            CHESS_FUZZ_CHECK(bestMove.has_value() == expectedBestMove.has_value());
            if (bestMove) {
                CHESS_FUZZ_CHECK(bestMove->move == expectedBestMove->move);
                CHESS_FUZZ_CHECK(bestMove->move.find(' ') == String::npos);
            }

            // IsNone is a statement about the spelling, so every word of the line is tried as one
            for (const StringView word : words) {
                const BestMove spelled{String(word)};
                CHESS_FUZZ_CHECK(spelled.IsNone() == (word == "(none)" || word == "0000"));
            }
            CHESS_FUZZ_CHECK(BestMove{}.IsNone());
        }

        void CheckCollector(const Lines& lines, int lineCount) {
            LineCollector collector(lineCount);
            LineCollector again(lineCount);
            LineCollector withBestMoves(lineCount);
            for (const StringView line : lines) {
                collector.Offer(line);
                again.Offer(line);
                withBestMoves.Offer(line);
                withBestMoves.Offer("bestmove e2e4 ponder e7e5");
            }

            const Vector<Info> kept = collector.Lines();
            int previous = 0;
            for (const Info& info : kept) {
                CHESS_FUZZ_CHECK(info.score.has_value());
                CHESS_FUZZ_CHECK(info.multiPv >= 1 && info.multiPv <= lineCount);
                CHESS_FUZZ_CHECK(info.multiPv > previous);
                previous = info.multiPv;
            }
            CHESS_FUZZ_CHECK(SameInfos(kept, again.Lines()));
            CHESS_FUZZ_CHECK(SameInfos(kept, withBestMoves.Lines()));
            CHESS_FUZZ_CHECK(SameInfos(kept, ExpectedKept(lines, lineCount)));
        }

    }  // namespace

    void FuzzUciOutput(const uint8_t* data, size_t size) {
        const Lines lines = SplitLines(ViewOf(data, size));
        for (const StringView line : lines) {
            CheckLine(line);
        }
        for (const int lineCount : kLineCounts) {
            CheckCollector(lines, lineCount);
        }
    }

    bool UciOutputIsAccepted(const uint8_t* data, size_t size) {
        for (const StringView line : SplitLines(ViewOf(data, size))) {
            if (UciProtocol::ParseInfo(line) || UciProtocol::ParseBestMove(line)) {
                return true;
            }
        }
        return false;
    }

}  // namespace BECore::Chess::Fuzzing
