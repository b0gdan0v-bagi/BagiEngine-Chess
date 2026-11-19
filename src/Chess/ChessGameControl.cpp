#include "ChessGameControl.h"

#include <BECore/Control/Json.h>
#include <Chess/Engine/OpponentChoice.h>
#include <Chess/Game/CapturedMaterial.h>
#include <Chess/Game/EditorText.h>
#include <Chess/Game/GameEnding.h>
#include <Chess/Game/MoveList.h>
#include <Chess/Game/TakeBack.h>

namespace BECore::Chess {

    namespace {

        /// Indexed by PieceType; '.' is None
        constexpr char kWhiteLetters[] = ".PNBRQK";
        constexpr char kBlackLetters[] = ".pnbrqk";
        static_assert(sizeof(kWhiteLetters) - 1 == static_cast<size_t>(PieceType::King) + 1, "one letter per piece type and one for None");

        constexpr StringView kPromotionLetters = "nbrq";
        constexpr StringView kMoveType = "move";
        constexpr size_t kUciMoveLength = 4;  // "e2e4"; a promotion has one letter more
        constexpr size_t kUciPromotionLength = 5;

        StringView StatusName(GameStatus status) {
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
                case GameStatus::Resigned:
                    return "resigned";
                case GameStatus::DrawAgreed:
                    return "draw_agreed";
            }
            return "ongoing";
        }

        StringView ErrorKindName(EngineErrorKind kind) {
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

        Json TextJson(const String& text) {
            return MakeJsonString(StringView(text));
        }

        bool IsUciSyntax(StringView text) {
            if (text.size() != kUciMoveLength && text.size() != kUciPromotionLength) {
                return false;
            }
            const auto isSquare = [&text](size_t at) { return text[at] >= 'a' && text[at] <= 'h' && text[at + 1] >= '1' && text[at + 1] <= '8'; };
            if (!isSquare(0) || !isSquare(2)) {
                return false;
            }
            return text.size() == kUciMoveLength || kPromotionLetters.find(text[kUciMoveLength]) != StringView::npos;
        }

        /// Whether the legal moves have a promotion between the squares of @p uci (a four letter spelling)
        bool NeedsPromotionPiece(const Position& position, StringView uci) {
            for (const Move move : MoveGenerator::GenerateLegal(position)) {
                const String spelled = move.ToUci();
                if (move.IsPromotion() && StringView(spelled.data(), kUciMoveLength) == uci) {
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

        Json MaterialJson(const Position& position) {
            const MaterialSummary summary = SummarizeMaterial(position);
            const auto captured = [&](PieceColor gone) {
                Json counts = Json::object();
                for (const PieceType type : CapturableTypes()) {
                    const StringView id = CapturedId(type);
                    counts[ToStdString(id)] = summary.GetMissing(gone, type);
                }
                return counts;
            };

            Json material = Json::object();
            material["balance"] = summary.GetBalance();
            material["capturedByWhite"] = captured(PieceColor::Black);  // what White has taken is what Black is missing
            material["capturedByBlack"] = captured(PieceColor::White);
            return material;
        }

        String LevelIds() {
            String ids;
            for (const OpponentLevel& level : GetOpponentLevels()) {
                ids += Format("{}{}", ids.empty() ? "" : ", ", level.id);
            }
            return ids;
        }

        /// The opponent as set_state asks for it, checked
        struct OpponentPick {
            ProgramSides plays;
            SeatChoice white;
            SeatChoice black;
            Optional<uint32_t> seed;  ///< Empty: the game's seed stays
        };

        constexpr StringView kBothId = "both";

        String EngineIds() {
            String ids;
            for (const OpponentEngine engine : EnumUtils<OpponentEngine>::Values()) {
                ids += Format("{}{}", ids.empty() ? "" : ", ", OpponentEngineId(engine));
            }
            return ids;
        }

        Json ErrorJson(const Optional<EngineError>& error) {
            if (!error) {
                return Json();
            }
            Json described = Json::object();
            described["kind"] = MakeJsonString(ErrorKindName(error->kind));
            described["detail"] = TextJson(error->detail);
            return described;
        }

        Json ScoreJson(const AnalysisScore& score) {
            Json json = Json::object();
            json["kind"] = score.kind == AnalysisScoreKind::Mate ? "mate" : "centipawns";
            json["value"] = score.value;
            return json;
        }

        struct AnalysisPick {
            Optional<bool> evaluation;
            Optional<bool> hint;
            Optional<bool> ratings;
        };

        constexpr const char* kAnalysisUsage = "'analysis' has to be an object like {\"evaluation\": true, \"hint\": true, \"ratings\": true} (the members are optional booleans)";

        std::expected<AnalysisPick, ControlError> ParseAnalysis(const Json& analysis) {
            if (!analysis.is_object()) {
                return std::unexpected(MakeControlError(ControlErrors::kInvalidParams, kAnalysisUsage));
            }
            AnalysisPick pick;
            for (const auto& [key, value] : analysis.items()) {
                Optional<bool>* target = key == "evaluation" ? &pick.evaluation : key == "hint" ? &pick.hint : key == "ratings" ? &pick.ratings : nullptr;
                if (!target || !value.is_boolean()) {
                    return std::unexpected(MakeControlError(ControlErrors::kInvalidParams, kAnalysisUsage));
                }
                *target = value.get<bool>();
            }
            return pick;
        }

        constexpr const char* kResignUsage =
            "'resign' has to be an object like {} (the person's color, or the side to move with two people) or {\"color\": \"white\"} (the member is optional, \"white\" or \"black\")";

        constexpr const char* kDrawUsage = "'draw' has to be an empty object {}: the person offers a draw (nothing else is a member of it)";

        constexpr const char* kPuzzleUsage = "'puzzle' has to be an object with exactly one member: {\"index\": 0} or {\"id\": \"00sHx\"} begins a puzzle, {\"next\": true}, {\"retry\": true} or "
                                             "{\"end\": true} acts on the one that is on";

        constexpr const char* kPuzzleOnUsage = "a puzzle is on: leave it first with {\"puzzle\": {\"end\": true}} (a takeback, a resignation or a draw offer would break its line)";

        constexpr const char* kTakeBackUsage = "'takeBack' has to be an object like {} (the button's rule) or {\"plies\": 2} (the member is an optional integer from 1 to the moves played)";

        /// The SAN of every move of the game, in the order of GetMoves()
        Json SanMovesJson(const ChessGameModel& model) {
            Json san = Json::array();
            for (const MoveListRow& row : BuildMoveList(model.GetStartFen(), model.GetMoves())) {
                for (const MoveListEntry* entry : {&row.white, &row.black}) {
                    if (entry->ply >= 0) {
                        san.push_back(TextJson(entry->san));
                    }
                }
            }
            return san;
        }

        StringView PuzzlePhaseId(PuzzlePhase phase) {
            switch (phase) {
                case PuzzlePhase::YourMove:
                    return "your_move";
                case PuzzlePhase::Replying:
                    return "replying";
                case PuzzlePhase::Solved:
                    return "solved";
            }
            return "your_move";
        }

        /// {"eco", "name"} of the opening of the game, or null
        Json OpeningJson(const OpeningBook& openings, const ChessGameModel& model) {
            const Optional<OpeningEntry> entry = OpeningOf(openings, model.GetStartFen(), model.GetMoves(), model.GetMoves().size());
            if (!entry) {
                return Json();
            }
            Json opening = Json::object();
            opening["eco"] = TextJson(entry->eco);
            opening["name"] = TextJson(entry->name);
            return opening;
        }

        ControlError BadOpponent(String message) {
            return MakeControlError(ControlErrors::kInvalidParams, message);
        }

        /// Reads "level" and "engine" of the opponent or of one of its seat objects (named @p where in a message); a member that is missing keeps what it was
        std::expected<void, ControlError> ReadChoice(const Json& object, StringView where, SeatChoice& choice) {
            if (FindJsonMember(object, "level")) {
                const auto id = GetJsonString(object, "level");
                const Optional<size_t> level = id ? FindOpponentLevel(StringView(id->data(), id->size())) : std::nullopt;
                if (!level) {
                    return std::unexpected(BadOpponent(Format("{} 'level' has to be the id of a level: {}", where, LevelIds())));
                }
                choice.level = *level;
            }
            if (FindJsonMember(object, "engine")) {
                const auto id = GetJsonString(object, "engine");
                const Optional<OpponentEngine> engine = id ? FindOpponentEngine(StringView(id->data(), id->size())) : std::nullopt;
                if (!engine) {
                    return std::unexpected(BadOpponent(Format("{} 'engine' has to be the id of an engine: {}", where, EngineIds())));
                }
                choice.engine = *engine;
            }
            return {};
        }

        /// Nothing but "engine" and "level" is a member of a seat object
        std::expected<void, ControlError> ReadSeat(const Json& seat, StringView color, SeatChoice& choice) {
            const String where = Format("opponent '{}'", color);
            if (!seat.is_object()) {
                return std::unexpected(BadOpponent(Format("{} has to be an object like {{\"engine\": \"own\", \"level\": \"easy\"}}", where)));
            }
            for (const auto& [key, value] : seat.items()) {
                if (key != "engine" && key != "level") {
                    return std::unexpected(BadOpponent(Format("{} has no member '{}': only 'engine' and 'level'", where, key)));
                }
            }
            return ReadChoice(seat, StringView(where), choice);
        }

        /// Reads {"plays", "level", "engine", "white", "black", "seed"}; a member that is missing keeps what @p current says, per seat. "level" and "engine" are for both seats, then the seat objects
        /// override one seat, then "plays" says who plays. Nothing is changed: the pick is applied by the caller once everything is valid
        std::expected<OpponentPick, ControlError> ParseOpponent(const Json& opponent, OpponentPick current) {
            if (!opponent.is_object()) {
                return std::unexpected(BadOpponent("'opponent' has to be an object like {\"plays\": \"black\", \"level\": \"easy\"}"));
            }
            for (SeatChoice* seat : {&current.white, &current.black}) {  // one level and one engine for both seats
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
                    const StringView id = side ? StringView(side->data(), side->size()) : StringView();
                    const Optional<PieceColor> color = side ? ParseColor(id) : std::nullopt;
                    if (side && id == kBothId) {
                        current.plays = ProgramSides{true, true};
                    } else if (color) {
                        current.plays = ProgramSides::Of(color);
                    } else {
                        return std::unexpected(BadOpponent("opponent 'plays' has to be \"white\", \"black\", \"both\" or null (two people): the colors the program plays"));
                    }
                }
            }
            if (FindJsonMember(opponent, "seed")) {
                const Optional<int64_t> seed = GetJsonInt(opponent, "seed");
                if (!seed || *seed < 0 || *seed > static_cast<int64_t>(std::numeric_limits<uint32_t>::max())) {
                    return std::unexpected(BadOpponent(Format("opponent 'seed' has to be an integer from 0 to {} (0 is no variety)", std::numeric_limits<uint32_t>::max())));
                }
                current.seed = static_cast<uint32_t>(*seed);
            }
            return current;
        }

    }  // namespace

    ChessGameControl::ChessGameControl(ChessGameModel& model, OpponentController& opponent, PositionAnalyst& analyst, DrawOfferJudge& drawJudge, const OpeningBook& openings,
                                       const PositionEditor& editor, PuzzleSession& puzzle)
        : _model(model), _opponent(opponent), _analyst(analyst), _drawJudge(drawJudge), _openings(openings), _editor(editor), _puzzle(puzzle) {}

    MoveList ChessGameControl::LegalMoves() const {
        return _model.IsGameOver() ? MoveList() : MoveGenerator::GenerateLegal(_model.GetPosition());
    }

    std::expected<Json, ControlError> ChessGameControl::GetState() const {
        Json board = Json::array();
        for (int8_t rank = 7; rank >= 0; --rank) {
            String line;
            for (int8_t file = 0; file < 8; ++file) {
                const Piece piece = _model.GetPieceAt(Square{file, rank});
                const char* letters = piece.color == PieceColor::Black ? kBlackLetters : kWhiteLetters;
                line.push_back(letters[static_cast<size_t>(piece.type)]);
            }
            board.push_back(MakeJsonString(line));
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
        state["sanMoves"] = SanMovesJson(_model);
        state["opening"] = OpeningJson(_openings, _model);
        state["takeBackPlies"] = _puzzle.IsActiveFor(_model) ? 0 : TakeBackPlies(_model.GetMoves().size(), _model.GetTurn(), _opponent.GetPlays());
        state["opponent"] = OpponentJson();
        state["material"] = MaterialJson(_model.GetPosition());
        state["analysis"] = AnalysisJson();
        state["drawOffer"] = DrawOfferJson();
        state["editor"] = EditorJson();
        state["puzzle"] = PuzzleJson();
        return state;
    }

    Json ChessGameControl::PuzzleJson() const {
        const Puzzle* puzzle = _puzzle.GetCurrent();
        if (puzzle == nullptr || !_puzzle.IsActiveFor(_model)) {
            return Json();
        }
        Json themes = Json::array();
        for (const String& theme : puzzle->themes) {
            themes.push_back(TextJson(theme));
        }
        Json described = Json::object();
        described["id"] = TextJson(puzzle->id);
        described["index"] = *_puzzle.GetIndex();
        described["count"] = _puzzle.GetPuzzles().Size();
        described["rating"] = puzzle->rating;
        described["themes"] = std::move(themes);
        described["solver"] = MakeJsonString(PieceColorId(puzzle->SolverColor()));
        described["state"] = MakeJsonString(PuzzlePhaseId(_puzzle.GetPhase(_model)));
        described["mistakes"] = _puzzle.GetMistakes();
        described["wrong"] = _puzzle.WasWrong();
        described["step"] = _puzzle.GetStep(_model);
        described["steps"] = _puzzle.GetSteps();
        return described;
    }

    Json ChessGameControl::EditorJson() const {
        if (!_model.IsDrafting()) {
            return Json();
        }
        const auto built = _editor.Build();
        const EditorTool tool = _editor.GetTool();

        Json described = Json::object();
        described["fen"] = TextJson(_editor.ToFen());
        described["error"] = built ? Json() : MakeJsonString(EditorText::Refusal(built.error()));
        Json picked = Json::object();
        picked["piece"] = MakeJsonString(EditorToolId(tool.type));
        picked["color"] = tool.IsEraser() ? Json() : MakeJsonString(PieceColorId(tool.color));
        described["tool"] = std::move(picked);
        return described;
    }

    Json ChessGameControl::OpponentJson() const {
        const Optional<PieceColor> plays = _opponent.GetPlays();
        const Optional<EngineError>& error = _opponent.GetError();

        Json seats = Json::object();
        for (const PieceColor color : {PieceColor::White, PieceColor::Black}) {
            const SeatChoice& choice = _opponent.GetSeatChoice(color);
            Json seat = Json::object();
            seat["engine"] = MakeJsonString(OpponentEngineId(choice.engine));
            seat["level"] = MakeJsonString(GetOpponentLevels()[choice.level].id);
            const StringView id = PieceColorId(color);
            seats[ToStdString(id)] = std::move(seat);
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
        opponent["seed"] = _model.GetSeed();
        opponent["thinking"] = _opponent.IsThinking();
        opponent["error"] = ErrorJson(error);
        return opponent;
    }

    Json ChessGameControl::DrawOfferJson() const {
        const DrawOfferState offer = _drawJudge.GetState(_model);
        if (offer == DrawOfferState::Idle) {
            return Json();
        }
        Json described = Json::object();
        described["state"] = offer == DrawOfferState::Pending ? "pending" : "declined";
        return described;
    }

    Json ChessGameControl::AnalysisJson() const {
        Json analysis = Json::object();
        analysis["evaluationOn"] = _analyst.IsEvaluationOn();
        analysis["hintRequested"] = _analyst.IsHintRequested(_model);
        analysis["thinking"] = _analyst.IsThinking();

        const Optional<AnalysisLine> evaluation = _analyst.GetEvaluation(_model);
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

        const Optional<AnalysisLine> hint = _analyst.GetHint(_model);
        if (hint) {
            Json line = Json::object();
            line["move"] = TextJson(hint->move.ToUci());
            line["text"] = TextJson(ScoreText(hint->score));  // as the engine gave it: the side to move's view
            line["depth"] = hint->depth;
            analysis["hint"] = std::move(line);
        } else {
            analysis["hint"] = Json();
        }

        Json lines = Json::array();
        for (const AnalysisLine& line : _analyst.GetLines(_model)) {
            const AnalysisScore white = ScoreForWhite(line.score, _model.GetTurn());
            Json described = Json::object();
            described["move"] = TextJson(line.move.ToUci());
            described["score"] = ScoreJson(white);
            described["text"] = TextJson(ScoreText(white));
            described["depth"] = line.depth;
            lines.push_back(std::move(described));
        }
        analysis["lines"] = std::move(lines);

        // One entry per move of "moves", in the same order: null while the move is not rated
        analysis["ratingsOn"] = _analyst.AreRatingsOn();
        Json ratings = Json::array();
        for (const Optional<MoveRating>& rating : _analyst.GetRatings(_model)) {
            if (rating) {
                Json described = Json::object();
                described["brilliant"] = rating->IsBrilliant();
                described["reason"] = MakeJsonString(MoveRatingReasonId(rating->reason));
                ratings.push_back(std::move(described));
            } else {
                ratings.push_back(Json());
            }
        }
        analysis["ratings"] = std::move(ratings);
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
        if (!uci || !IsUciSyntax(StringView(uci->data(), uci->size()))) {
            return std::unexpected(MakeControlError(ControlErrors::kInvalidParams, "'uci' has to be a move like \"e2e4\" or, for a promotion, \"e7e8q\" (the piece is n, b, r or q)"));
        }
        const StringView spelling(uci->data(), uci->size());

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
        const Json* takeBackMember = FindJsonMember(state, "takeBack");
        const Json* resignMember = FindJsonMember(state, "resign");
        const Json* drawMember = FindJsonMember(state, "draw");
        const Json* puzzleMember = FindJsonMember(state, "puzzle");
        if (!fenMember && !opponentMember && !analysisMember && !takeBackMember && !resignMember && !drawMember && !puzzleMember) {
            return std::unexpected(
                MakeControlError(ControlErrors::kInvalidParams,
                                 "'fen' (a FEN string), 'opponent' ({\"plays\", \"level\", \"engine\", \"white\", \"black\", \"seed\"}), both, or "
                                 "'analysis' ({\"evaluation\", \"hint\", \"ratings\"}), 'takeBack' ({\"plies\"}), 'resign' ({\"color\"}), 'draw' ({}) or 'puzzle' ({\"index\"}) alone are needed"));
        }
        if (puzzleMember) {
            if (fenMember || opponentMember || analysisMember || takeBackMember || resignMember || drawMember) {
                return std::unexpected(MakeControlError(ControlErrors::kInvalidParams, "'puzzle' cannot be combined with another member: a puzzle is a game of its own"));
            }
            return SetPuzzle(*puzzleMember);
        }
        if (takeBackMember) {
            if (fenMember || opponentMember || analysisMember || resignMember || drawMember) {
                return std::unexpected(MakeControlError(ControlErrors::kInvalidParams, "'takeBack' cannot be combined with another member: what it takes back depends on the game they change"));
            }
            return SetTakeBack(*takeBackMember);
        }
        if (resignMember) {
            if (fenMember || opponentMember || analysisMember || drawMember) {
                return std::unexpected(
                    MakeControlError(ControlErrors::kInvalidParams, "'resign' cannot be combined with another member: who resigns depends on the game and the opponent they change"));
            }
            return SetResign(*resignMember);
        }
        if (drawMember) {
            if (fenMember || opponentMember || analysisMember) {
                return std::unexpected(MakeControlError(ControlErrors::kInvalidParams, "'draw' cannot be combined with another member: the offer is judged for the game and the opponent they change"));
            }
            return SetDraw(*drawMember);
        }
        if (analysisMember && (fenMember || opponentMember)) {
            return std::unexpected(MakeControlError(ControlErrors::kInvalidParams,
                                                    "'analysis' cannot be combined with 'fen' or 'opponent': a hint belongs to the position after them, so set them in one call and ask in the next"));
        }

        // Check everything first: a refused call changes nothing
        Optional<Position> position;
        if (fenMember) {
            const auto fen = GetJsonString(state, "fen");
            if (!fen) {
                return std::unexpected(MakeControlError(ControlErrors::kInvalidParams, "'fen' has to be a FEN string"));
            }
            const auto parsed = Position::FromFen(StringView(fen->data(), fen->size()));
            if (!parsed) {
                return std::unexpected(MakeControlError(ControlErrors::kInvalidParams, Format("not a position this game can play: {}", EnumUtils<FenError>::ToString(parsed.error()))));
            }
            position = *parsed;
        }
        Optional<OpponentPick> pick;
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
            if (pick->seed) {
                _model.SetSeed(*pick->seed);  // after the pick that can fail, before the position
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
            if (const Optional<HintRefusal> refusal = HintRefusalFor(_model, _opponent.GetSides())) {
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
        if (pick->ratings) {
            _analyst.SetRatings(*pick->ratings);  // no refusal: a finished game's moves are rated like any
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

    std::expected<void, ControlError> ChessGameControl::SetTakeBack(const Json& takeBack) {
        if (_puzzle.IsActiveFor(_model)) {
            return std::unexpected(MakeControlError(ControlErrors::kInvalidParams, kPuzzleOnUsage));
        }
        if (!takeBack.is_object()) {
            return std::unexpected(MakeControlError(ControlErrors::kInvalidParams, kTakeBackUsage));
        }
        for (const auto& member : takeBack.items()) {
            if (member.key() != "plies") {
                return std::unexpected(MakeControlError(ControlErrors::kInvalidParams, kTakeBackUsage));
            }
        }
        const size_t played = _model.GetMoves().size();
        size_t plies = TakeBackPlies(played, _model.GetTurn(), _opponent.GetPlays());
        if (FindJsonMember(takeBack, "plies")) {
            const Optional<int64_t> asked = GetJsonInt(takeBack, "plies");
            if (!asked || *asked < 1 || static_cast<uint64_t>(*asked) > played) {
                return std::unexpected(MakeControlError(ControlErrors::kInvalidParams, Format("'plies' has to be an integer from 1 to the {} moves played", played)));
            }
            plies = static_cast<size_t>(*asked);
        } else if (plies == 0) {
            return std::unexpected(MakeControlError(ControlErrors::kInvalidParams, played == 0 ? "there is no move to take back" : "the only move is the program's: the person has none to take back"));
        }
        _model.TakeBack(plies);
        return {};
    }

    std::expected<void, ControlError> ChessGameControl::SetResign(const Json& resign) {
        if (_puzzle.IsActiveFor(_model)) {
            return std::unexpected(MakeControlError(ControlErrors::kInvalidParams, kPuzzleOnUsage));
        }
        if (!resign.is_object()) {
            return std::unexpected(MakeControlError(ControlErrors::kInvalidParams, kResignUsage));
        }
        for (const auto& member : resign.items()) {
            if (member.key() != "color") {
                return std::unexpected(MakeControlError(ControlErrors::kInvalidParams, kResignUsage));
            }
        }
        Optional<PieceColor> named;
        if (FindJsonMember(resign, "color")) {
            const auto text = GetJsonString(resign, "color");
            named = text ? ParseColor(StringView(text->data(), text->size())) : std::nullopt;
            if (!named) {
                return std::unexpected(MakeControlError(ControlErrors::kInvalidParams, kResignUsage));
            }
        }
        if (_model.IsGameOver()) {
            return std::unexpected(MakeControlError(ChessControlErrors::kGameOver, Format("the game is over ({}): there is nobody to resign", StatusName(_model.GetStatus()))));
        }
        const ProgramSides sides = _opponent.GetSides();
        const Optional<PieceColor> color = named ? named : ResigningColor(_model.GetTurn(), _opponent.GetPlays(), sides.white && sides.black);
        if (!color) {
            return std::unexpected(MakeControlError(ControlErrors::kInvalidParams, "two programs play: name the color that resigns, {\"color\": \"white\"} or {\"color\": \"black\"}"));
        }
        const auto resigned = _model.Resign(*color);
        EXPECT(resigned.has_value(), "ChessGameControl: a game that is not over refuses to resign");
        return {};
    }

    std::expected<void, ControlError> ChessGameControl::SetDraw(const Json& draw) {
        if (_puzzle.IsActiveFor(_model)) {
            return std::unexpected(MakeControlError(ControlErrors::kInvalidParams, kPuzzleOnUsage));
        }
        if (!draw.is_object() || !draw.empty()) {
            return std::unexpected(MakeControlError(ControlErrors::kInvalidParams, kDrawUsage));
        }
        const std::expected<void, DrawOfferRefusal> offered = PersonOffersDraw(_model, _opponent.GetSides(), _drawJudge);
        if (offered) {
            return {};
        }
        switch (offered.error()) {
            case DrawOfferRefusal::GameOver:
                return std::unexpected(MakeControlError(ChessControlErrors::kGameOver, Format("the game is over ({}): there is nothing to offer a draw in", StatusName(_model.GetStatus()))));
            case DrawOfferRefusal::NotYourTurn:
                return std::unexpected(MakeControlError(ControlErrors::kInvalidParams, "the program plays the side to move: a draw is offered on the person's turn"));
            case DrawOfferRefusal::NoPerson:
                return std::unexpected(MakeControlError(ControlErrors::kInvalidParams, "two programs play: nobody is there to offer a draw"));
            case DrawOfferRefusal::Puzzle:
                return std::unexpected(MakeControlError(ControlErrors::kInvalidParams, "a puzzle is on: there is no draw to offer in it"));
        }
        return std::unexpected(MakeControlError(ControlErrors::kInvalidParams, kDrawUsage));
    }

    std::expected<void, ControlError> ChessGameControl::SetPuzzle(const Json& puzzle) {
        if (!puzzle.is_object() || puzzle.size() != 1) {
            return std::unexpected(MakeControlError(ControlErrors::kInvalidParams, kPuzzleUsage));
        }
        const auto member = puzzle.items().begin();
        const String key = String(member.key().c_str(), member.key().size());
        const PuzzleSet& puzzles = _puzzle.GetPuzzles();
        if (key == "index" || key == "id") {
            Optional<size_t> index;
            if (key == "index") {
                const Optional<int64_t> asked = GetJsonInt(puzzle, "index");
                if (!asked || *asked < 0 || static_cast<uint64_t>(*asked) >= puzzles.Size()) {
                    return std::unexpected(MakeControlError(ControlErrors::kInvalidParams, Format("'index' has to be an integer below the {} puzzles of the set", puzzles.Size())));
                }
                index = static_cast<size_t>(*asked);
            } else {
                const auto id = GetJsonString(puzzle, "id");
                index = id ? puzzles.FindIndexById(StringView(id->data(), id->size())) : std::nullopt;
                if (!index) {
                    return std::unexpected(MakeControlError(ControlErrors::kInvalidParams, "'id' has to be the id of a puzzle of the set"));
                }
            }
            const bool begun = _puzzle.Begin(_model, *index);
            EXPECT(begun, "ChessGameControl: a puzzle of the set does not begin");
            return {};
        }
        const Json& value = member.value();
        if ((key != "next" && key != "retry" && key != "end") || !value.is_boolean() || !value.get<bool>()) {
            return std::unexpected(MakeControlError(ControlErrors::kInvalidParams, kPuzzleUsage));
        }
        if (!_puzzle.IsActiveFor(_model)) {
            return std::unexpected(MakeControlError(ControlErrors::kInvalidParams, Format("'{}' needs a puzzle that is on: begin one with {{\"index\": 0}}", key)));
        }
        if (key == "next") {
            static_cast<void>(_puzzle.Next(_model));
        } else if (key == "retry") {
            static_cast<void>(_puzzle.Retry(_model));
        } else {
            static_cast<void>(_puzzle.Leave(_model));
        }
        return {};
    }

    uint64_t ChessGameControl::GetStateVersion() const {
        return _model.GetVersion() + _opponent.GetRevision() + _analyst.GetRevision() + _drawJudge.GetRevision() + _puzzle.GetRevision() + _editor.GetRevision();
    }

}  // namespace BECore::Chess
