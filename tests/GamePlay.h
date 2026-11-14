#pragma once

#include "SeededRandom.h"
#include "TestEngines.h"

#include <Chess/Engine/ChessEngine.h>
#include <Chess/Engine/SearchRequest.h>
#include <Chess/Game/ChessGameModel.h>
#include <Chess/Game/MoveGenerator.h>

/// Whole games of the tests: who picks the next move (a Player) and the loop that plays two of them against each other through a ChessGameModel
namespace BECore::Chess::Tests {

    /// A game of the tests is stopped after this many plies, if it has not ended, unless the test says otherwise
    inline constexpr size_t kDefaultPlyCap = 120;

    /// What a player of a test game is: the next move for the side to move of @p model
    using Player = std::function<Move(const ChessGameModel&)>;

    /// Asks @p engine with @p limits through SearchRequest::FromModel (the history goes with the request). An error fails the test and gives a null move, which the model refuses
    inline Player EnginePlayer(IChessEngine& engine, SearchLimits limits) {
        return [&engine, limits](const ChessGameModel& model) {
            const auto move = engine.FindMove(SearchRequest::FromModel(model, limits), CancellationToken::Create());
            EXPECT_TRUE(move.has_value()) << DescribeError(move ? std::nullopt : Optional<EngineError>(move.error())).c_str();
            return move.value_or(Move{});
        };
    }

    /// A random legal move from @p random (seeded, so a game is the same on every run and platform); @p random must outlive the Player
    inline Player RandomPlayer(SeededRandom& random) {
        return [&random](const ChessGameModel& model) {
            const MoveList moves = MoveGenerator::GenerateLegal(model.GetPosition());
            return moves[random.Below(moves.size())];
        };
    }

    /// Plays @p white and @p black against each other until the game ends or @p plyCap plies; every move must be accepted. Returns the moves in UCI
    inline Vector<eastl::string> PlayGame(ChessGameModel& model, const Player& white, const Player& black, size_t plyCap = kDefaultPlyCap) {
        Vector<eastl::string> played;
        while (!model.IsGameOver() && model.GetMoves().size() < plyCap) {
            const Move move = (model.GetTurn() == PieceColor::White ? white : black)(model);
            const auto accepted = model.TryMove(move);
            EXPECT_TRUE(accepted.has_value()) << "move " << move.ToUci().c_str() << " in " << model.GetPosition().ToFen().c_str();
            if (!accepted) {
                break;
            }
            played.push_back(move.ToUci());
        }
        return played;
    }

}  // namespace BECore::Chess::Tests
