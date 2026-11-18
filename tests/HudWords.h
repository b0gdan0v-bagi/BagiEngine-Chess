#pragma once

#include <BECore/GameManager/CoreManager.h>
#include <Chess/Components/ChessHudComponent.h>
#include <Chess/Engine/OpponentText.h>
#include <Chess/Game/ChessGameModel.h>
#include <Chess/Game/MoveGenerator.h>
#include <Chess/Game/StatusText.h>
#include <EASTL/algorithm.h>
#include <gtest/gtest.h>
#include <tests/ChessTestUtil.h>
#include <tests/ShippedOpenings.h>

/// Every word the HUD of Chess can show, taken from the code that makes them (StatusText, OpponentText and its tables), never typed again here: a new word, level or kind
/// of error reaches the tests of the layout by itself. The font is proportional, so the longest word by characters is not always the widest: the layout tests walk all of them
namespace BECore::Chess::Tests {

    struct HudWords {
        Vector<String> turn;           ///< `{turn}`: whose move it is, who won, "Computer is thinking"
        Vector<String> status;         ///< `{status}`: the status of the game ("" included), the line of a view and every sentence of an engine error
        Vector<String> opening;        ///< `{opening}`: "" and the longest display names of the shipped book
        Vector<String> opponentError;  ///< `{opponentError}` of the opponent panel: "" and every sentence of an engine error

        /// How many rounds cover every word: round i takes the word i of each list (the shorter lists wrap round). The elements of the HUD lay out independently of one another,
        /// so a word needs a round with some word of the others, not with each of them
        [[nodiscard]] size_t Rounds() const {
            return std::max({turn.size(), status.size(), opening.size(), opponentError.size()});
        }

        /// Sets `turn`, `status`, `opening` and `opponentError` to the words of round @p round
        void Publish(size_t round) const {
            UIVars& vars = CoreManager::GetUIVars();
            vars.Set("turn"_intern, View(turn, round));
            vars.Set("status"_intern, View(status, round));
            vars.Set("opening"_intern, View(opening, round));
            vars.Set("opponentError"_intern, View(opponentError, round));
        }

    private:
        static StringView View(const Vector<String>& words, size_t round) {
            const String& word = words[round % words.size()];
            return StringView(word);
        }
    };

    namespace HudWordsDetail {

        constexpr size_t kLongestOpenings = 24;  ///< how many display names of the book the layout tests lay out

        inline void Add(Vector<String>& words, String word) {
            if (eastl::find(words.begin(), words.end(), word) == words.end()) {
                words.push_back(std::move(word));
            }
        }

        inline void Play(ChessGameModel& model, StringView uci) {
            const auto move = MoveGenerator::ParseUci(model.GetPosition(), uci);
            EXPECT_TRUE(move.has_value() && model.TryMove(*move).has_value()) << "a move of the scratch game: " << String(uci.data(), uci.size());
        }

        /// What the HUD says about the model as it stands; @p seen gets the status of the game, so AllHudWords() can tell that every status was reached
        inline void Collect(HudWords& words, Vector<GameStatus>& seen, const ChessGameModel& model) {
            if (eastl::find(seen.begin(), seen.end(), model.GetStatus()) == seen.end()) {
                seen.push_back(model.GetStatus());
            }
            Add(words.turn, StatusText::Turn(model, false));
            Add(words.status, StatusText::Status(model));
        }

        inline size_t CodePoints(const String& text) {
            return static_cast<size_t>(eastl::count_if(text.begin(), text.end(), [](char c) { return (static_cast<unsigned char>(c) & 0xC0) != 0x80; }));
        }

        /// The names of the shipped table (second column), the distinct display names of them in the order of the file
        inline Vector<String> ShippedDisplayNames() {
            const String& table = ShippedOpeningsTable();
            Vector<String> names;
            HashSet<String> seen;
            size_t lineStart = table.find('\n') + 1;  // the header line
            while (lineStart < table.size()) {
                size_t lineEnd = table.find('\n', lineStart);
                if (lineEnd == String::npos) {
                    lineEnd = table.size();
                }
                const size_t firstTab = table.find('\t', lineStart);
                const size_t secondTab = firstTab < lineEnd ? table.find('\t', firstTab + 1) : String::npos;
                if (secondTab < lineEnd) {
                    String display = OpeningDisplayName(StringView(table.data() + firstTab + 1, secondTab - firstTab - 1), kOpeningMaxChars);
                    if (seen.insert(display).second) {
                        names.push_back(std::move(display));
                    }
                }
                lineStart = lineEnd + 1;
            }
            return names;
        }

        /// A spanning set of the display names: "" and the @p count longest by characters (ties by the order of the file), so the rounds of the layout tests stay few
        inline void AddLongestOpenings(Vector<String>& words, size_t count) {
            Add(words, {});
            Vector<String> names = ShippedDisplayNames();
            for (size_t taken = 0; taken < count && !names.empty(); ++taken) {
                size_t best = 0;
                for (size_t i = 1; i < names.size(); ++i) {
                    if (CodePoints(names[i]) > CodePoints(names[best])) {
                        best = i;
                    }
                }
                Add(words, std::move(names[best]));
                names.erase(names.begin() + static_cast<ptrdiff_t>(best));
            }
        }

    }  // namespace HudWordsDetail

    /// The words of the HUD, from a scratch game of their own that reaches every status and views its start and a move, then the thinking form of the turn, the declined draw and the sentence of every
    /// kind of engine error (also the `opponentError` list: the variable of the panel's error line). The names of the segments of the panel (modes, colors, engines, levels) are not here: they are
    /// elements of the scene that a test walks, and a test holds their text to the tables
    inline HudWords AllHudWords() {
        using namespace HudWordsDetail;
        HudWords words;
        Vector<GameStatus> seen;

        ChessGameModel model;
        Collect(words, seen, model);
        Play(model, "e2e4");
        Collect(words, seen, model);

        model.SetPosition(PositionFromFen("4k3/4r3/8/8/8/8/8/4K3 w - - 0 1"));
        Collect(words, seen, model);

        model.SetPosition(PositionFromFen("6k1/5ppp/8/8/8/8/5PPP/R5K1 w - - 0 1"));
        Play(model, "a1a8");
        Collect(words, seen, model);

        model.SetPosition(PositionFromFen("r5k1/5ppp/8/8/8/8/5PPP/6K1 b - - 0 1"));
        Play(model, "a8a1");
        Collect(words, seen, model);

        model.SetPosition(PositionFromFen("7k/5Q2/6K1/8/8/8/8/8 b - - 0 1"));
        Collect(words, seen, model);
        model.SetPosition(PositionFromFen("8/8/4k3/8/8/4K3/8/8 w - - 0 1"));
        Collect(words, seen, model);
        model.SetPosition(PositionFromFen("4k3/8/8/8/8/8/8/R3K3 w - - 100 80"));
        Collect(words, seen, model);
        model.Reset();
        for (int cycle = 0; cycle < 2; ++cycle) {
            for (const char* uci : {"g1f3", "g8f6", "f3g1", "f6g8"}) {
                Play(model, uci);
            }
        }
        Collect(words, seen, model);

        for (const PieceColor loser : {PieceColor::White, PieceColor::Black}) {
            model.Reset();
            EXPECT_TRUE(model.Resign(loser).has_value());
            Collect(words, seen, model);
        }
        model.Reset();
        EXPECT_TRUE(model.AgreeDraw().has_value());
        Collect(words, seen, model);

        // A new status of the game must get a position above, or its words would stay out of the matrix
        for (const GameStatus status : EnumUtils<GameStatus>::Values()) {
            EXPECT_NE(eastl::find(seen.begin(), seen.end(), status), seen.end()) << "no position of the scratch game reaches " << static_cast<int>(status);
        }

        // The line of a view, with the widest move number the game is held to (three digits, like the number cell of the move panel)
        model.SetPosition(PositionFromFen("rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 999"));
        Play(model, "e2e4");
        for (const size_t shown : {size_t{0}, size_t{1}}) {
            EXPECT_TRUE(model.ShowMoves(shown));
            Add(words.status, StatusText::Status(model));
        }

        model.Reset();
        Add(words.turn, StatusText::Turn(model, true));

        Add(words.status, StatusText::DrawDeclined());
        for (const EngineErrorKind kind : EnumUtils<EngineErrorKind>::Values()) {
            Add(words.status, OpponentText::Error(EngineError{kind, {}}));
        }
        AddLongestOpenings(words.opening, kLongestOpenings);
        Add(words.opponentError, {});
        for (const EngineErrorKind kind : EnumUtils<EngineErrorKind>::Values()) {
            Add(words.opponentError, OpponentText::Error(EngineError{kind, {}}));
        }
        return words;
    }

}  // namespace BECore::Chess::Tests
