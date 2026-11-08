#include "ChessGameControl.h"

#include <Chess/Engine/OpponentChoice.h>
#include <Chess/Game/CapturedMaterial.h>

namespace BECore::Chess {

    namespace {

        /// One letter per PieceType, in enum order; '.' is an empty square. White is upper case.
        constexpr char kWhiteLetters[] = ".PNBRQK";
        constexpr char kBlackLetters[] = ".pnbrqk";
        static_assert(sizeof(kWhiteLetters) - 1 == static_cast<size_t>(PieceType::King) + 1, "one letter per piece type and one for None");

        constexpr eastl::string_view kPromotionLetters = "nbrq";
        constexpr eastl::string_view kMoveType = "move";
        constexpr size_t kUciMoveLength = 4;  // "e2e4"; a promotion has one letter more
        constexpr size_t kUciPromotionLength = 5;

        eastl::string_view StatusName(GameStatus status) {
            switch (status) {
                case GameStatus::Ongoing:
                    return "ongoing";
                case GameStatus::Checkmate:
                    return "checkmate";
                case GameStatus::Stalemate:
                    return "stalemate";
                case GameStatus::DrawInsufficientMaterial:
                    return "draw_insufficient_material";
                case GameStatus::DrawThreefold:
                    return "draw_threefold";
                case GameStatus::DrawFiftyMoves:
                    return "draw_fifty_moves";
            }
            return "ongoing";
        }

        /// The kind of an engine error as it goes on the wire
        eastl::string_view ErrorKindName(EngineErrorKind kind) {
            switch (kind) {
                case EngineErrorKind::BadRequest:
                    return "bad_request";
                case EngineErrorKind::LaunchFailed:
                    return "launch_failed";
                case EngineErrorKind::Timeout:
                    return "timeout";
                case EngineErrorKind::ProcessDied:
                    return "process_died";
                case EngineErrorKind::ProtocolError:
                    return "protocol_error";
                case EngineErrorKind::NoMove:
                    return "no_move";
                case EngineErrorKind::IllegalMove:
                    return "illegal_move";
                case EngineErrorKind::Cancelled:
                    return "cancelled";
                case EngineErrorKind::Unsupported:
                    return "unsupported";
            }
            return "bad_request";
        }

        Json TextJson(const eastl::string& text) {
            return MakeJsonString(eastl::string_view(text.data(), text.size()));
        }

        /// Whether @p text is spelled like a UCI move: two squares and, for a promotion, the piece in lower case
        bool IsUciSyntax(eastl::string_view text) {
            if (text.size() != kUciMoveLength && text.size() != kUciPromotionLength) {
                return false;
            }
            const auto isSquare = [&text](size_t at) { return text[at] >= 'a' && text[at] <= 'h' && text[at + 1] >= '1' && text[at + 1] <= '8'; };
            if (!isSquare(0) || !isSquare(2)) {
                return false;
            }
            return text.size() == kUciMoveLength || kPromotionLetters.find(text[kUciMoveLength]) != eastl::string_view::npos;
        }

        /// Whether the legal moves have a promotion between the squares of @p uci (a four letter spelling)
        bool NeedsPromotionPiece(const Position& position, eastl::string_view uci) {
            for (const Move move : MoveGenerator::GenerateLegal(position)) {
                const eastl::string spelled = move.ToUci();
                if (move.IsPromotion() && eastl::string_view(spelled.data(), kUciMoveLength) == uci) {
                    return true;
                }
            }
            return false;
        }

        /// The members that say how the game stands; the state and the outcome of a move both carry them
        void AddStanding(Json& json, const ChessGameModel& model) {
            const auto winner = model.GetWinner();
            json["turn"] = MakeJsonString(PieceColorId(model.GetTurn()));
            json["status"] = MakeJsonString(StatusName(model.GetStatus()));
            json["winner"] = winner ? MakeJsonString(PieceColorId(*winner)) : Json();
            json["inCheck"] = model.IsInCheck();
        }

        /// {"balance", "capturedByWhite", "capturedByBlack"} of the position: a derived view, computed at every call (it has no version of its own)
        Json MaterialJson(const Position& position) {
            const MaterialSummary summary = SummarizeMaterial(position);
            const auto captured = [&](PieceColor gone) {
                Json counts = Json::object();
                for (const PieceType type : CapturableTypes()) {
                    const eastl::string_view id = CapturedId(type);
                    counts[std::string(id.data(), id.size())] = summary.GetMissing(gone, type);
                }
                return counts;
            };

            Json material = Json::object();
            material["balance"] = summary.GetBalance();
            material["capturedByWhite"] = captured(PieceColor::Black);  // what White has taken is what Black is missing
            material["capturedByBlack"] = captured(PieceColor::White);
            return material;
        }

        /// "easy, medium, hard, max" for a message
        eastl::string LevelIds() {
            eastl::string ids;
            for (const OpponentLevel& level : GetOpponentLevels()) {
                ids += Format("{}{}", ids.empty() ? "" : ", ", level.id);
            }
            return ids;
        }

        /// The opponent as set_state asks for it, checked: the colors the program plays and the choice of each seat
        struct OpponentPick {
            ProgramSides plays;
            SeatChoice white;
            SeatChoice black;
        };

        constexpr eastl::string_view kBothId = "both";

        /// "stockfish, own" for a message
        eastl::string EngineIds() {
            eastl::string ids;
            for (const OpponentEngine engine : EnumUtils<OpponentEngine>::Values()) {
                ids += Format("{}{}", ids.empty() ? "" : ", ", OpponentEngineId(engine));
            }
            return ids;
        }

        /// {"kind", "detail"} of an engine error, or null
        Json ErrorJson(const std::optional<EngineError>& error) {
            if (!error) {
                return Json();
            }
            Json described = Json::object();
            described["kind"] = MakeJsonString(ErrorKindName(error->kind));
            described["detail"] = TextJson(error->detail);
            return described;
        }

        /// {"kind", "value"} of a score
        Json ScoreJson(const AnalysisScore& score) {
            Json json = Json::object();
            json["kind"] = score.kind == AnalysisScoreKind::Mate ? "mate" : "centipawns";
            json["value"] = score.value;
            return json;
        }

        /// What set_state asks of the analyst, checked; a member that is missing is nothing
        struct AnalysisPick {
            std::optional<bool> evaluation;
            std::optional<bool> hint;
        };

        constexpr const char* kAnalysisUsage = "'analysis' has to be an object like {\"evaluation\": true, \"hint\": true} (both members are optional booleans)";

        /// Reads {"evaluation", "hint"}; nothing is changed
        std::expected<AnalysisPick, ControlError> ParseAnalysis(const Json& analysis) {
            if (!analysis.is_object()) {
                return std::unexpected(MakeControlError(ControlErrors::kInvalidParams, kAnalysisUsage));
            }
            AnalysisPick pick;
            for (const auto& [key, value] : analysis.items()) {
                std::optional<bool>* target = key == "evaluation" ? &pick.evaluation : key == "hint" ? &pick.hint : nullptr;
                if (!target || !value.is_boolean()) {
                    return std::unexpected(MakeControlError(ControlErrors::kInvalidParams, kAnalysisUsage));
                }
                *target = value.get<bool>();
            }
            return pick;
        }

        ControlError BadOpponent(eastl::string message) {
            return MakeControlError(ControlErrors::kInvalidParams, message);
        }

        /// Reads "level" and "engine" of @p object (the opponent, or one of its seat objects, named @p where in a message) into @p choice; a member that is missing keeps what it was
        std::expected<void, ControlError> ReadChoice(const Json& object, eastl::string_view where, SeatChoice& choice) {
            if (FindJsonMember(object, "level")) {
                const auto id = GetJsonString(object, "level");
                const std::optional<size_t> level = id ? FindOpponentLevel(eastl::string_view(id->data(), id->size())) : std::nullopt;
                if (!level) {
                    return std::unexpected(BadOpponent(Format("{} 'level' has to be the id of a level: {}", where, LevelIds())));
                }
                choice.level = *level;
            }
            if (FindJsonMember(object, "engine")) {
                const auto id = GetJsonString(object, "engine");
                const std::optional<OpponentEngine> engine = id ? FindOpponentEngine(eastl::string_view(id->data(), id->size())) : std::nullopt;
                if (!engine) {
                    return std::unexpected(BadOpponent(Format("{} 'engine' has to be the id of an engine: {}", where, EngineIds())));
                }
                choice.engine = *engine;
            }
            return {};
        }

        /// Reads the seat object {"engine", "level"} of one color into @p choice; nothing else is a member of it
        std::expected<void, ControlError> ReadSeat(const Json& seat, eastl::string_view color, SeatChoice& choice) {
            const eastl::string where = Format("opponent '{}'", color);
            if (!seat.is_object()) {
                return std::unexpected(BadOpponent(Format("{} has to be an object like {{\"engine\": \"own\", \"level\": \"easy\"}}", where)));
            }
            for (const auto& [key, value] : seat.items()) {
                if (key != "engine" && key != "level") {
                    return std::unexpected(BadOpponent(Format("{} has no member '{}': only 'engine' and 'level'", where, key)));
                }
            }
            return ReadChoice(seat, eastl::string_view(where.data(), where.size()), choice);
        }

        /// Reads {"plays", "level", "engine", "white", "black"}; a member that is missing keeps what @p current says, per seat. "level" and "engine" are for both seats, then the seat objects
        /// override one seat, then "plays" says who plays. Nothing is changed: the pick is applied by the caller once everything is valid
        std::expected<OpponentPick, ControlError> ParseOpponent(const Json& opponent, OpponentPick current) {
            if (!opponent.is_object()) {
                return std::unexpected(BadOpponent("'opponent' has to be an object like {\"plays\": \"black\", \"level\": \"easy\"}"));
            }
            for (SeatChoice* seat : {&current.white, &current.black}) {  // the old shape: one level and one engine for both seats
                if (auto read = ReadChoice(opponent, "opponent", *seat); !read) {
                    return std::unexpected(std::move(read.error()));
                }
            }
            if (const Json* white = FindJsonMember(opponent, "white")) {
                if (auto read = ReadSeat(*white, "white", current.white); !read) {
                    return std::unexpected(std::move(read.error()));
                }
            }
            if (const Json* black = FindJsonMember(opponent, "black")) {
                if (auto read = ReadSeat(*black, "black", current.black); !read) {
                    return std::unexpected(std::move(read.error()));
                }
            }
            if (const Json* plays = FindJsonMember(opponent, "plays")) {
                if (plays->is_null()) {
                    current.plays = ProgramSides{};
                } else {
                    const auto side = GetJsonString(opponent, "plays");
                    const eastl::string_view id = side ? eastl::string_view(side->data(), side->size()) : eastl::string_view();
                    const std::optional<PieceColor> color = side ? ParseColor(id) : std::nullopt;
                    if (side && id == kBothId) {
                        current.plays = ProgramSides{true, true};
                    } else if (color) {
                        current.plays = ProgramSides::Of(color);
                    } else {
                        return std::unexpected(BadOpponent("opponent 'plays' has to be \"white\", \"black\", \"both\" or null (two people): the colors the program plays"));
                    }
                }
            }
            return current;
        }

    }  // namespace

    ChessGameControl::ChessGameControl(ChessGameModel& model, OpponentController& opponent, PositionAnalyst& analyst) : _model(model), _opponent(opponent), _analyst(analyst) {}

    MoveList ChessGameControl::LegalMoves() const {
        return _model.IsGameOver() ? MoveList() : MoveGenerator::GenerateLegal(_model.GetPosition());
    }

    std::expected<Json, ControlError> ChessGameControl::GetState() const {
        Json board = Json::array();
        for (int8_t rank = 7; rank >= 0; --rank) {
            std::string line;
            for (int8_t file = 0; file < 8; ++file) {
                const Piece piece = _model.GetPieceAt(Square{file, rank});
                const char* letters = piece.color == PieceColor::Black ? kBlackLetters : kWhiteLetters;
                line.push_back(letters[static_cast<size_t>(piece.type)]);
            }
            board.push_back(std::move(line));
        }

        Json moves = Json::array();
        for (const Move move : _model.GetMoves()) {
            moves.push_back(TextJson(move.ToUci()));
        }

        Json state = Json::object();
        state["fen"] = TextJson(_model.GetPosition().ToFen());
        state["board"] = std::move(board);
        AddStanding(state, _model);
        state["legalMoveCount"] = LegalMoves().size();
        state["startFen"] = TextJson(_model.GetStartFen());
        state["moves"] = std::move(moves);
        state["opponent"] = OpponentJson();
        state["material"] = MaterialJson(_model.GetPosition());
        state["analysis"] = AnalysisJson();
        return state;
    }

    Json ChessGameControl::OpponentJson() const {
        const std::optional<PieceColor> plays = _opponent.GetPlays();
        const std::optional<EngineError>& error = _opponent.GetError();

        Json seats = Json::object();
        for (const PieceColor color : {PieceColor::White, PieceColor::Black}) {
            const SeatChoice& choice = _opponent.GetSeatChoice(color);
            Json seat = Json::object();
            seat["engine"] = MakeJsonString(OpponentEngineId(choice.engine));
            seat["level"] = MakeJsonString(GetOpponentLevels()[choice.level].id);
            const eastl::string_view id = PieceColorId(color);
            seats[std::string(id.data(), id.size())] = std::move(seat);
        }

        Json opponent = Json::object();
        if (_opponent.IsWatching()) {
            opponent["plays"] = MakeJsonString(kBothId);
        } else {
            opponent["plays"] = plays ? MakeJsonString(PieceColorId(*plays)) : Json();
        }
        opponent["level"] = MakeJsonString(GetOpponentLevels()[_opponent.GetLevel()].id);
        opponent["engine"] = MakeJsonString(OpponentEngineId(_opponent.GetEngine()));
        opponent["seats"] = std::move(seats);
        opponent["thinking"] = _opponent.IsThinking();
        opponent["error"] = ErrorJson(error);
        return opponent;
    }

    Json ChessGameControl::AnalysisJson() const {
        Json analysis = Json::object();
        analysis["evaluationOn"] = _analyst.IsEvaluationOn();
        analysis["hintRequested"] = _analyst.IsHintRequested(_model);
        analysis["thinking"] = _analyst.IsThinking();

        const std::optional<AnalysisLine> evaluation = _analyst.GetEvaluation(_model);
        if (evaluation) {
            const AnalysisScore white = ScoreForWhite(evaluation->score, _model.GetTurn());  // a bar needs one fixed side
            Json line = Json::object();
            line["score"] = ScoreJson(white);
            line["text"] = TextJson(ScoreText(white));
            line["depth"] = evaluation->depth;
            analysis["evaluation"] = std::move(line);
        } else {
            analysis["evaluation"] = Json();
        }

        const std::optional<AnalysisLine> hint = _analyst.GetHint(_model);
        if (hint) {
            Json line = Json::object();
            line["move"] = TextJson(hint->move.ToUci());
            line["text"] = TextJson(ScoreText(hint->score));  // as the engine gave it: the side to move's view
            line["depth"] = hint->depth;
            analysis["hint"] = std::move(line);
        } else {
            analysis["hint"] = Json();
        }
        analysis["error"] = ErrorJson(_analyst.GetError());
        return analysis;
    }

    std::expected<Json, ControlError> ChessGameControl::GetActions() const {
        Json actions = Json::array();
        for (const Move move : LegalMoves()) {
            Json action = Json::object();
            action["type"] = MakeJsonString(kMoveType);
            action["uci"] = TextJson(move.ToUci());
            actions.push_back(std::move(action));
        }
        return actions;
    }

    std::expected<Json, ControlError> ChessGameControl::PerformAction(const Json& action) {
        const auto type = GetJsonString(action, "type");
        if (!type || *type != kMoveType) {
            return std::unexpected(MakeControlError(ControlErrors::kInvalidParams, "'type' has to be \"move\""));
        }
        const auto uci = GetJsonString(action, "uci");
        if (!uci || !IsUciSyntax(eastl::string_view(uci->data(), uci->size()))) {
            return std::unexpected(MakeControlError(ControlErrors::kInvalidParams, "'uci' has to be a move like \"e2e4\" or, for a promotion, \"e7e8q\" (the piece is n, b, r or q)"));
        }
        const eastl::string_view spelling(uci->data(), uci->size());

        if (_model.IsGameOver()) {
            return std::unexpected(MakeControlError(ChessControlErrors::kGameOver, Format("the game is over ({}); reset it or set a position", StatusName(_model.GetStatus()))));
        }

        const auto move = MoveGenerator::ParseUci(_model.GetPosition(), spelling);
        if (!move) {
            if (spelling.size() == kUciMoveLength && NeedsPromotionPiece(_model.GetPosition(), spelling)) {
                return std::unexpected(MakeControlError(ChessControlErrors::kPromotionRequired, Format("'{}' promotes a pawn: add the piece, '{}q' for a queen", spelling, spelling)));
            }
            return std::unexpected(MakeControlError(GameControlErrors::kIllegalAction, Format("'{}' is not a legal move in this position", spelling)));
        }

        const auto played = _model.TryMove(*move);
        EXPECT(played.has_value(), "ChessGameControl: ParseUci and TryMove disagree");

        Json outcome = Json::object();
        outcome["move"] = TextJson(move->ToUci());
        AddStanding(outcome, _model);
        return outcome;
    }

    std::expected<void, ControlError> ChessGameControl::Reset(const Json&) {
        _model.Reset();
        return {};
    }

    std::expected<void, ControlError> ChessGameControl::SetState(const Json& state) {
        const Json* fenMember = FindJsonMember(state, "fen");
        const Json* opponentMember = FindJsonMember(state, "opponent");
        const Json* analysisMember = FindJsonMember(state, "analysis");
        if (!fenMember && !opponentMember && !analysisMember) {
            return std::unexpected(MakeControlError(ControlErrors::kInvalidParams,
                                                    "'fen' (a FEN string), 'opponent' ({\"plays\", \"level\", \"engine\"}), both, or 'analysis' ({\"evaluation\", \"hint\"}) alone are needed"));
        }
        if (analysisMember && (fenMember || opponentMember)) {
            return std::unexpected(MakeControlError(ControlErrors::kInvalidParams,
                                                    "'analysis' cannot be combined with 'fen' or 'opponent': a hint belongs to the position after them, so set them in one call and ask in the next"));
        }

        // Check everything first: a refused call changes nothing
        std::optional<Position> position;
        if (fenMember) {
            const auto fen = GetJsonString(state, "fen");
            if (!fen) {
                return std::unexpected(MakeControlError(ControlErrors::kInvalidParams, "'fen' has to be a FEN string"));
            }
            const auto parsed = Position::FromFen(eastl::string_view(fen->data(), fen->size()));
            if (!parsed) {
                return std::unexpected(MakeControlError(ControlErrors::kInvalidParams, Format("not a position this game can play: {}", EnumUtils<FenError>::ToString(parsed.error()))));
            }
            position = *parsed;
        }
        std::optional<OpponentPick> pick;
        if (opponentMember) {
            auto parsed = ParseOpponent(*opponentMember, OpponentPick{_opponent.GetSides(), _opponent.GetSeatChoice(PieceColor::White), _opponent.GetSeatChoice(PieceColor::Black)});
            if (!parsed) {
                return std::unexpected(std::move(parsed.error()));
            }
            pick = *parsed;
        }

        if (analysisMember) {
            return SetAnalysis(*analysisMember);
        }

        // The opponent first: it is the one step that can fail later (no engine), and then the position stays as it is
        if (pick) {
            const auto chosen = _opponent.ChooseSeats(pick->plays, pick->white, pick->black);
            if (!chosen) {
                return std::unexpected(
                    MakeControlError(ChessControlErrors::kEngineUnavailable, Format("no engine for the opponent ({}): {}", ErrorKindName(chosen.error().kind), chosen.error().detail)));
            }
            _analyst.SetPicked(_opponent.GetEngine());  // at once, so a get_state before the next frame does not show the old engine's answer
        }
        if (position) {
            _model.SetPosition(*position);
        }
        return {};
    }

    std::expected<void, ControlError> ChessGameControl::SetAnalysis(const Json& analysis) {
        // Check everything first: a refused call changes nothing, the switch of the evaluation included
        const auto pick = ParseAnalysis(analysis);
        if (!pick) {
            return std::unexpected(pick.error());
        }
        if (pick->hint.value_or(false)) {
            if (const std::optional<HintRefusal> refusal = HintRefusalFor(_model, _opponent.GetSides())) {
                if (*refusal == HintRefusal::GameOver) {
                    return std::unexpected(MakeControlError(ChessControlErrors::kGameOver, Format("the game is over ({}): there is nothing to hint", StatusName(_model.GetStatus()))));
                }
                return std::unexpected(
                    MakeControlError(ChessControlErrors::kNotYourTurn, "the program plays this side: a hint is for the person, ask on the person's turn (never in a game of two programs)"));
            }
        }

        _analyst.SetPicked(_opponent.GetEngine());  // the engine the analysis will use is the one picked now
        if (pick->evaluation) {
            _analyst.SetEvaluation(*pick->evaluation);
        }
        if (pick->hint) {
            if (*pick->hint) {
                const auto asked = _analyst.RequestHint(_model);
                EXPECT(asked.has_value(), "ChessGameControl: HintRefusalFor and RequestHint disagree");
            } else {
                _analyst.CancelHint();
            }
        }
        return {};
    }

    uint64_t ChessGameControl::GetStateVersion() const {
        return _model.GetVersion() + _opponent.GetRevision() + _analyst.GetRevision();
    }

}  // namespace BECore::Chess
