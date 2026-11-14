#include "ChessSaveState.h"

#include <Generated/ChessSaveState.gen.hpp>

namespace BECore::Chess {

    namespace {

        constexpr StringView kNobodyId = "none";
        constexpr StringView kBothId = "both";
        constexpr char kMoveSeparator = ' ';

        std::unexpected<SaveError> Corrupt(const String& reason) {
            return std::unexpected(SaveError{SaveErrorKind::Corrupt, Format("the saved {}", reason)});
        }

        StringView PlaysId(ProgramSides sides) {
            if (sides.white && sides.black) {
                return kBothId;
            }
            if (sides.white) {
                return PieceColorId(PieceColor::White);
            }
            if (sides.black) {
                return PieceColorId(PieceColor::Black);
            }
            return kNobodyId;
        }

        Optional<ProgramSides> ParsePlays(StringView id) {
            if (id == kNobodyId) {
                return ProgramSides{};
            }
            if (id == kBothId) {
                return ProgramSides{true, true};
            }
            if (const Optional<PieceColor> color = ParseColor(id)) {
                return ProgramSides::Of(*color);
            }
            return std::nullopt;
        }

        StringView View(const String& text) {
            return StringView(text);
        }

        String Own(StringView text) {
            return String(text.data(), text.size());
        }

        /// The moves of the file, played in their turn on a scratch model; an error names the number of the move, never its text
        std::expected<eastl::vector<Move>, SaveError> ReplayMoves(const String& text, ChessGameModel& scratch) {
            eastl::vector<Move> moves;
            if (text.empty()) {
                return moves;
            }
            const StringView all = View(text);
            size_t begin = 0;
            while (begin <= all.size()) {
                size_t end = all.find(kMoveSeparator, begin);
                if (end == StringView::npos) {
                    end = all.size();
                }
                const size_t number = moves.size() + 1;
                const Optional<Move> move = MoveGenerator::ParseUci(scratch.GetPosition(), all.substr(begin, end - begin));
                if (!move) {
                    return Corrupt(Format("move {} is not a legal move in its turn", number));
                }
                if (!scratch.TryMove(*move)) {
                    return Corrupt(Format("move {} comes after the end of the game", number));
                }
                moves.push_back(*move);
                begin = end + 1;
            }
            return moves;
        }

        std::expected<SeatChoice, SaveError> ParseSeat(const String& engineId, const String& levelId, StringView seat) {
            const Optional<OpponentEngine> engine = FindOpponentEngine(View(engineId));
            if (!engine) {
                return Corrupt(Format("{} engine is not an engine of this game", seat));
            }
            const Optional<size_t> level = FindOpponentLevel(View(levelId));
            if (!level) {
                return Corrupt(Format("{} level is not a level of this game", seat));
            }
            return SeatChoice{*engine, *level};
        }

    }  // namespace

    ChessSaveState Capture(const ChessGameModel& model, const OpponentController& controller, const OpponentChoice& choice) {
        ChessSaveState state;
        state.startFen = Own(model.GetStartFen());
        for (const Move move : model.GetMoves()) {
            if (!state.moves.empty()) {
                state.moves.push_back(kMoveSeparator);
            }
            state.moves += move.ToUci();
        }
        const SeatChoice white = controller.GetSeatChoice(PieceColor::White);
        const SeatChoice black = controller.GetSeatChoice(PieceColor::Black);
        state.plays = Own(PlaysId(controller.GetSides()));
        state.whiteEngine = Own(OpponentEngineId(white.engine));
        state.whiteLevel = Own(GetOpponentLevels()[white.level].id);
        state.blackEngine = Own(OpponentEngineId(black.engine));
        state.blackLevel = Own(GetOpponentLevels()[black.level].id);
        state.personColor = Own(choice.ColorId());
        return state;
    }

    std::expected<RestorePlan, SaveError> Validate(const ChessSaveState& state) {
        RestorePlan plan;

        const std::expected<Position, FenError> start = Position::FromFen(View(state.startFen));
        if (!start) {
            return Corrupt(Format("start position is not a position this game can play ({})", EnumUtils<FenError>::ToString(start.error())));
        }
        plan.start = *start;

        ChessGameModel scratch;
        scratch.SetPosition(plan.start);
        std::expected<eastl::vector<Move>, SaveError> moves = ReplayMoves(state.moves, scratch);
        if (!moves) {
            return std::unexpected(std::move(moves.error()));
        }
        plan.moves = std::move(*moves);
        plan.finished = scratch.IsGameOver();

        const Optional<ProgramSides> plays = ParsePlays(View(state.plays));
        if (!plays) {
            return Corrupt("colors the program plays are not none, white, black or both");
        }
        plan.plays = *plays;

        std::expected<SeatChoice, SaveError> white = ParseSeat(state.whiteEngine, state.whiteLevel, "white seat's");
        if (!white) {
            return std::unexpected(std::move(white.error()));
        }
        std::expected<SeatChoice, SaveError> black = ParseSeat(state.blackEngine, state.blackLevel, "black seat's");
        if (!black) {
            return std::unexpected(std::move(black.error()));
        }
        plan.white = *white;
        plan.black = *black;

        const Optional<PieceColor> person = ParseColor(View(state.personColor));
        if (!person) {
            return Corrupt("color of the person is not white or black");
        }
        plan.person = *person;
        const bool programPlaysOneSide = plan.plays.white != plan.plays.black;
        if (programPlaysOneSide && plan.plays.Plays(plan.person)) {
            return Corrupt("person and the program play the same color");
        }
        return plan;
    }

    void Apply(const RestorePlan& plan, ChessGameModel& model, OpponentController& controller, OpponentChoice& choice) {
        if (!plan.finished) {
            model.SetPosition(plan.start);
            for (const Move move : plan.moves) {
                [[maybe_unused]] const bool played = model.TryMove(move).has_value();
                EXPECT(played, "Chess save: a move that Validate() played does not play in the model");
            }
        }

        // The sides first and the person's color after, only when the program plays no single side: SetColor() on a program that plays would remake its opponent and write one seat's
        // choice into the other, and with one side played the color follows from it
        if (const std::expected<void, EngineError> picked = controller.ChooseSeats(plan.plays, plan.white, plan.black); !picked) {
            LOG_WARNING("Save: the saved opponent was not restored, the game goes on as two people: {}"_format(picked.error().detail));
        }
        if (!controller.GetPlays()) {
            [[maybe_unused]] const auto colored = choice.SetColor(PieceColorId(plan.person));
        }
    }

}  // namespace BECore::Chess
