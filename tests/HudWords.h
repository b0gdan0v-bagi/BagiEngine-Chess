#pragma once

#include "ChessTestUtil.h"

#include <BECore/GameManager/CoreManager.h>
#include <Chess/Engine/OpponentText.h>
#include <Chess/Game/ChessGameModel.h>
#include <Chess/Game/MoveGenerator.h>
#include <Chess/Game/StatusText.h>
#include <EASTL/algorithm.h>
#include <gtest/gtest.h>

/// Every word the HUD of Chess can show, taken from the code that makes them (StatusText, OpponentText and its tables), never typed again here: a new word, level or kind
/// of error reaches the tests of the layout by itself. The font is proportional, so the longest word by characters is not always the widest: the layout tests walk all of them
namespace BECore::Chess::Tests {

    struct HudWords {
        eastl::vector<eastl::string> turn;           ///< `{turn}`: whose move it is, who won, "Computer is thinking"
        eastl::vector<eastl::string> status;         ///< `{status}`: the status of the game ("" included) and every sentence of an engine error
        eastl::vector<eastl::string> opponentError;  ///< `{opponentError}` of the opponent panel: "" and every sentence of an engine error

        /// How many rounds cover every word: round i takes the word i of each list (the shorter lists wrap round). The elements of the HUD lay out independently of one another,
        /// so a word needs a round with some word of the others, not with each of them
        [[nodiscard]] size_t Rounds() const {
            return std::max({turn.size(), status.size(), opponentError.size()});
        }

        /// Sets `turn`, `status` and `opponentError` to the words of round @p round
        void Publish(size_t round) const {
            UIVars& vars = CoreManager::GetUIVars();
            vars.Set("turn"_intern, View(turn, round));
            vars.Set("status"_intern, View(status, round));
            vars.Set("opponentError"_intern, View(opponentError, round));
        }

    private:
        static StringView View(const eastl::vector<eastl::string>& words, size_t round) {
            const eastl::string& word = words[round % words.size()];
            return StringView(word);
        }
    };

    namespace HudWordsDetail {

        inline void Add(eastl::vector<eastl::string>& words, eastl::string word) {
            if (eastl::find(words.begin(), words.end(), word) == words.end()) {
                words.push_back(std::move(word));
            }
        }

        inline void Play(ChessGameModel& model, StringView uci) {
            const auto move = MoveGenerator::ParseUci(model.GetPosition(), uci);
            EXPECT_TRUE(move.has_value() && model.TryMove(*move).has_value()) << "a move of the scratch game: " << std::string(uci.data(), uci.size());
        }

        /// What the HUD says about the model as it stands; @p seen gets the status of the game, so AllHudWords() can tell that every status was reached
        inline void Collect(HudWords& words, eastl::vector<GameStatus>& seen, const ChessGameModel& model) {
            if (eastl::find(seen.begin(), seen.end(), model.GetStatus()) == seen.end()) {
                seen.push_back(model.GetStatus());
            }
            Add(words.turn, StatusText::Turn(model, false));
            Add(words.status, StatusText::Status(model));
        }

    }  // namespace HudWordsDetail

    /// The words of the HUD, from a scratch game of their own: the opening, a move later, a check, a mate for each side, a stalemate and the three draws, then the
    /// thinking form of the turn, the sentence of every kind of engine error and the sentences of the error line of the panel. The names of the segments of the
    /// panel (modes, colors, engines, levels) are not here: they are elements of the scene that a test walks, and a test holds their text to the tables
    inline HudWords AllHudWords() {
        using namespace HudWordsDetail;
        HudWords words;
        eastl::vector<GameStatus> seen;

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

        // A new status of the game must get a position above, or its words would stay out of the matrix
        for (const GameStatus status : EnumUtils<GameStatus>::Values()) {
            EXPECT_NE(eastl::find(seen.begin(), seen.end(), status), seen.end()) << "no position of the scratch game reaches " << static_cast<int>(status);
        }

        model.Reset();
        Add(words.turn, StatusText::Turn(model, true));

        for (const EngineErrorKind kind : EnumUtils<EngineErrorKind>::Values()) {
            Add(words.status, OpponentText::Error(EngineError{kind, {}}));
        }
        Add(words.opponentError, {});
        for (const EngineErrorKind kind : EnumUtils<EngineErrorKind>::Values()) {
            Add(words.opponentError, OpponentText::Error(EngineError{kind, {}}));
        }
        return words;
    }

}  // namespace BECore::Chess::Tests
