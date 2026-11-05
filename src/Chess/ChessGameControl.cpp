#include "ChessGameControl.h"

#include <Chess/Engine/OpponentChoice.h>

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

        /// "easy, medium, hard, max" for a message
        eastl::string LevelIds() {
            eastl::string ids;
            for (const OpponentLevel& level : GetOpponentLevels()) {
                ids += Format("{}{}", ids.empty() ? "" : ", ", level.id);
            }
            return ids;
        }

        /// The opponent as set_state asks for it, checked
        struct OpponentPick {
            std::optional<PieceColor> plays;
            size_t level = kDefaultOpponentLevel;
            OpponentEngine engine = kDefaultOpponentEngine;
        };

        /// "stockfish, own" for a message
        eastl::string EngineIds() {
            eastl::string ids;
            for (const OpponentEngine engine : EnumUtils<OpponentEngine>::Values()) {
                ids += Format("{}{}", ids.empty() ? "" : ", ", OpponentEngineId(engine));
            }
            return ids;
        }

        ControlError BadOpponent(eastl::string message) {
            return MakeControlError(ControlErrors::kInvalidParams, message);
        }

        /// Reads {"plays", "level", "engine"}; a member that is missing keeps what @p current says. Nothing is changed: the pick is applied by the caller once everything is valid
        std::expected<OpponentPick, ControlError> ParseOpponent(const Json& opponent, OpponentPick current) {
            if (!opponent.is_object()) {
                return std::unexpected(BadOpponent("'opponent' has to be an object like {\"plays\": \"black\", \"level\": \"easy\"}"));
            }
            if (const Json* plays = FindJsonMember(opponent, "plays")) {
                if (plays->is_null()) {
                    current.plays = std::nullopt;
                } else {
                    const auto side = GetJsonString(opponent, "plays");
                    const std::optional<PieceColor> color = side ? ParseColor(eastl::string_view(side->data(), side->size())) : std::nullopt;
                    if (!color) {
                        return std::unexpected(BadOpponent("opponent 'plays' has to be \"white\", \"black\" or null (two people): the color the program plays"));
                    }
                    current.plays = color;
                }
            }
            if (FindJsonMember(opponent, "level")) {
                const auto id = GetJsonString(opponent, "level");
                const std::optional<size_t> level = id ? FindOpponentLevel(eastl::string_view(id->data(), id->size())) : std::nullopt;
                if (!level) {
                    return std::unexpected(BadOpponent(Format("opponent 'level' has to be the id of a level: {}", LevelIds())));
                }
                current.level = *level;
            }
            if (FindJsonMember(opponent, "engine")) {
                const auto id = GetJsonString(opponent, "engine");
                const std::optional<OpponentEngine> engine = id ? FindOpponentEngine(eastl::string_view(id->data(), id->size())) : std::nullopt;
                if (!engine) {
                    return std::unexpected(BadOpponent(Format("opponent 'engine' has to be the id of an engine: {}", EngineIds())));
                }
                current.engine = *engine;
            }
            return current;
        }

    }  // namespace

    ChessGameControl::ChessGameControl(ChessGameModel& model, OpponentController& opponent) : _model(model), _opponent(opponent) {}

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
        return state;
    }

    Json ChessGameControl::OpponentJson() const {
        const std::optional<PieceColor> plays = _opponent.GetPlays();
        const std::optional<EngineError>& error = _opponent.GetError();

        Json opponent = Json::object();
        opponent["plays"] = plays ? MakeJsonString(PieceColorId(*plays)) : Json();
        opponent["level"] = MakeJsonString(GetOpponentLevels()[_opponent.GetLevel()].id);
        opponent["engine"] = MakeJsonString(OpponentEngineId(_opponent.GetEngine()));
        opponent["thinking"] = _opponent.IsThinking();
        if (error) {
            Json described = Json::object();
            described["kind"] = MakeJsonString(ErrorKindName(error->kind));
            described["detail"] = TextJson(error->detail);
            opponent["error"] = std::move(described);
        } else {
            opponent["error"] = Json();
        }
        return opponent;
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
        if (!fenMember && !opponentMember) {
            return std::unexpected(MakeControlError(ControlErrors::kInvalidParams, "'fen' (a FEN string), 'opponent' ({\"plays\", \"level\", \"engine\"}) or both are needed"));
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
            auto parsed = ParseOpponent(*opponentMember, OpponentPick{_opponent.GetPlays(), _opponent.GetLevel(), _opponent.GetEngine()});
            if (!parsed) {
                return std::unexpected(std::move(parsed.error()));
            }
            pick = *parsed;
        }

        // The opponent first: it is the one step that can fail later (no engine), and then the position stays as it is
        if (pick) {
            const auto chosen = _opponent.Choose(pick->plays, pick->level, pick->engine);
            if (!chosen) {
                return std::unexpected(
                    MakeControlError(ChessControlErrors::kEngineUnavailable, Format("no engine for the opponent ({}): {}", ErrorKindName(chosen.error().kind), chosen.error().detail)));
            }
        }
        if (position) {
            _model.SetPosition(*position);
        }
        return {};
    }

    uint64_t ChessGameControl::GetStateVersion() const {
        return _model.GetVersion() + _opponent.GetRevision();
    }

}  // namespace BECore::Chess
