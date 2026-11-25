#include "ChessGameControl.h"

#include <BECore/Control/ControlParams.h>
#include <Chess/Engine/OpponentChoice.h>
#include <Chess/Game/CapturedMaterial.h>
#include <Chess/Game/EditorText.h>
#include <Chess/Game/GameEnding.h>
#include <Chess/Game/MoveList.h>
#include <Chess/Game/TakeBack.h>
#include <Chess/Graphics/GraphicsPreset.h>

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

        void WriteText(JsonWriter& writer, StringView key, StringView text) {
            writer.Key(key);
            writer.Value(text);
        }

        void WriteText(JsonWriter& writer, StringView key, const String& text) {
            WriteText(writer, key, StringView(text));
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

        /// "winner" of the state and of the outcome of a move
        void WriteWinner(JsonWriter& writer, const ChessGameModel& model) {
            const auto winner = model.GetWinner();
            writer.Key("winner");
            if (winner) {
                writer.Value(PieceColorId(*winner));
            } else {
                writer.Null();
            }
        }

        /// One value: the counts of the pieces a side has taken, the ids in byte order
        void WriteCaptured(JsonWriter& writer, const MaterialSummary& summary, PieceColor gone) {
            struct Count {
                StringView id;
                int count;
            };
            Count counts[8];
            size_t used = 0;
            for (const PieceType type : CapturableTypes()) {
                ASSERT(used < std::size(counts), "WriteCaptured: more capturable types than room");
                const Count entry{CapturedId(type), static_cast<int>(summary.GetMissing(gone, type))};
                size_t at = used++;
                for (; at > 0 && entry.id < counts[at - 1].id; --at) {
                    counts[at] = counts[at - 1];
                }
                counts[at] = entry;
            }
            writer.BeginObject();
            for (size_t i = 0; i < used; ++i) {
                writer.Member(counts[i].id, counts[i].count);
            }
            writer.EndObject();
        }

        void WriteMaterial(JsonWriter& writer, const Position& position) {
            const MaterialSummary summary = SummarizeMaterial(position);
            writer.BeginObject();
            writer.Member("balance", summary.GetBalance());
            writer.Key("capturedByBlack");
            WriteCaptured(writer, summary, PieceColor::White);
            writer.Key("capturedByWhite");
            WriteCaptured(writer, summary, PieceColor::Black);  // what White has taken is what Black is missing
            writer.EndObject();
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

        void WriteError(JsonWriter& writer, const Optional<EngineError>& error) {
            if (!error) {
                writer.Null();
                return;
            }
            writer.BeginObject();
            WriteText(writer, "detail", error->detail);
            writer.Member("kind", ErrorKindName(error->kind));
            writer.EndObject();
        }

        void WriteScore(JsonWriter& writer, const AnalysisScore& score) {
            writer.BeginObject();
            writer.Member("kind", score.kind == AnalysisScoreKind::Mate ? "mate" : "centipawns");
            writer.Member("value", score.value);
            writer.EndObject();
        }

        struct AnalysisPick {
            Optional<bool> evaluation;
            Optional<bool> hint;
            Optional<bool> ratings;
        };

        constexpr const char* kAnalysisUsage = "'analysis' has to be an object like {\"evaluation\": true, \"hint\": true, \"ratings\": true} (the members are optional booleans)";

        std::expected<AnalysisPick, ControlError> ParseAnalysis(JsonView analysis) {
            if (!analysis.IsObject()) {
                return std::unexpected(MakeControlError(ControlErrors::kInvalidParams, kAnalysisUsage));
            }
            AnalysisPick pick;
            for (const JsonMember member : analysis.Members()) {
                Optional<bool>* target = member.key.TextEquals("evaluation") ? &pick.evaluation
                                         : member.key.TextEquals("hint")     ? &pick.hint
                                         : member.key.TextEquals("ratings")  ? &pick.ratings
                                                                             : nullptr;
                const Optional<bool> value = member.value.AsBool();
                if (!target || !value) {
                    return std::unexpected(MakeControlError(ControlErrors::kInvalidParams, kAnalysisUsage));
                }
                *target = value;
            }
            return pick;
        }

        constexpr const char* kResignUsage =
            "'resign' has to be an object like {} (the person's color, or the side to move with two people) or {\"color\": \"white\"} (the member is optional, \"white\" or \"black\")";

        constexpr const char* kDrawUsage = "'draw' has to be an empty object {}: the person offers a draw (nothing else is a member of it)";

        constexpr const char* kPuzzleUsage = "'puzzle' has to be an object with exactly one member: {\"index\": 0} or {\"id\": \"00sHx\"} begins a puzzle, {\"next\": true}, {\"retry\": true} or "
                                             "{\"end\": true} acts on the one that is on";

        constexpr const char* kReviewUsage = "'review' has to be an object with exactly one member: {\"start\": true} or {\"cancel\": true}";

        constexpr const char* kGraphicsUsage = "'graphics' has to be an object with exactly one member: {\"preset\": \"classic\"} or {\"preset\": \"beer\"}";

        constexpr const char* kPuzzleOnUsage = "a puzzle is on: leave it first with {\"puzzle\": {\"end\": true}} (a takeback, a resignation or a draw offer would break its line)";

        constexpr const char* kTakeBackUsage = "'takeBack' has to be an object like {} (the button's rule) or {\"plies\": 2} (the member is an optional integer from 1 to the moves played)";

        /// One value: the SAN of every move of the game, in the order of GetMoves()
        void WriteSanMoves(JsonWriter& writer, const ChessGameModel& model) {
            writer.BeginArray();
            for (const MoveListRow& row : BuildMoveList(model.GetStartFen(), model.GetMoves())) {
                for (const MoveListEntry* entry : {&row.white, &row.black}) {
                    if (entry->ply >= 0) {
                        writer.Value(StringView(entry->san));
                    }
                }
            }
            writer.EndArray();
        }

        /// The caller answers null for Idle first: an Idle review has no state to name
        StringView ReviewStateId(ReviewState state) {
            switch (state) {
                case ReviewState::Idle:
                    ASSERT(false, "ReviewStateId: an Idle review is null in the state, never named");
                    return "";
                case ReviewState::Running:
                    return "running";
                case ReviewState::Done:
                    return "done";
                case ReviewState::Failed:
                    return "failed";
            }
            return "";
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
        void WriteOpening(JsonWriter& writer, const OpeningBook& openings, const ChessGameModel& model) {
            const Optional<OpeningEntry> entry = OpeningOf(openings, model.GetStartFen(), model.GetMoves(), model.GetMoves().size());
            if (!entry) {
                writer.Null();
                return;
            }
            writer.BeginObject();
            WriteText(writer, "eco", entry->eco);
            WriteText(writer, "name", entry->name);
            writer.EndObject();
        }

        ControlError BadOpponent(String message) {
            return MakeControlError(ControlErrors::kInvalidParams, message);
        }

        /// Reads "level" and "engine" of the opponent or of one of its seat objects (named @p where in a message); a member that is missing keeps what it was
        std::expected<void, ControlError> ReadChoice(JsonView object, StringView where, SeatChoice& choice) {
            if (FindParam(object, "level")) {
                const auto id = GetParamString(object, "level");
                const Optional<size_t> level = id ? FindOpponentLevel(StringView(*id)) : std::nullopt;
                if (!level) {
                    return std::unexpected(BadOpponent(Format("{} 'level' has to be the id of a level: {}", where, LevelIds())));
                }
                choice.level = *level;
            }
            if (FindParam(object, "engine")) {
                const auto id = GetParamString(object, "engine");
                const Optional<OpponentEngine> engine = id ? FindOpponentEngine(StringView(*id)) : std::nullopt;
                if (!engine) {
                    return std::unexpected(BadOpponent(Format("{} 'engine' has to be the id of an engine: {}", where, EngineIds())));
                }
                choice.engine = *engine;
            }
            return {};
        }

        /// Nothing but "engine" and "level" is a member of a seat object
        std::expected<void, ControlError> ReadSeat(JsonView seat, StringView color, SeatChoice& choice) {
            const String where = Format("opponent '{}'", color);
            if (!seat.IsObject()) {
                return std::unexpected(BadOpponent(Format("{} has to be an object like {{\"engine\": \"own\", \"level\": \"easy\"}}", where)));
            }
            for (const JsonMember member : seat.Members()) {
                if (!member.key.TextEquals("engine") && !member.key.TextEquals("level")) {
                    String key;
                    member.key.ReadText(key);
                    return std::unexpected(BadOpponent(Format("{} has no member '{}': only 'engine' and 'level'", where, key)));
                }
            }
            return ReadChoice(seat, StringView(where), choice);
        }

        /// Reads {"plays", "level", "engine", "white", "black", "seed"}; a member that is missing keeps what @p current says, per seat. "level" and "engine" are for both seats, then the seat objects
        /// override one seat, then "plays" says who plays. Nothing is changed: the pick is applied by the caller once everything is valid
        std::expected<OpponentPick, ControlError> ParseOpponent(JsonView opponent, OpponentPick current) {
            if (!opponent.IsObject()) {
                return std::unexpected(BadOpponent("'opponent' has to be an object like {\"plays\": \"black\", \"level\": \"easy\"}"));
            }
            for (SeatChoice* seat : {&current.white, &current.black}) {  // one level and one engine for both seats
                if (auto read = ReadChoice(opponent, "opponent", *seat); !read) {
                    return std::unexpected(std::move(read.error()));
                }
            }
            if (const Optional<JsonView> white = FindParam(opponent, "white")) {
                if (auto read = ReadSeat(*white, "white", current.white); !read) {
                    return std::unexpected(std::move(read.error()));
                }
            }
            if (const Optional<JsonView> black = FindParam(opponent, "black")) {
                if (auto read = ReadSeat(*black, "black", current.black); !read) {
                    return std::unexpected(std::move(read.error()));
                }
            }
            if (const Optional<JsonView> plays = FindParam(opponent, "plays")) {
                if (plays->IsNull()) {
                    current.plays = ProgramSides{};
                } else {
                    const auto side = GetParamString(opponent, "plays");
                    const StringView id = side ? StringView(*side) : StringView();
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
            if (FindParam(opponent, "seed")) {
                const Optional<int64_t> seed = GetParamInt(opponent, "seed");
                if (!seed || *seed < 0 || *seed > static_cast<int64_t>(std::numeric_limits<uint32_t>::max())) {
                    return std::unexpected(BadOpponent(Format("opponent 'seed' has to be an integer from 0 to {} (0 is no variety)", std::numeric_limits<uint32_t>::max())));
                }
                current.seed = static_cast<uint32_t>(*seed);
            }
            return current;
        }

    }  // namespace

    ChessGameControl::ChessGameControl(ChessGameModel& model, OpponentController& opponent, PositionAnalyst& analyst, DrawOfferJudge& drawJudge, const OpeningBook& openings,
                                       const PositionEditor& editor, PuzzleSession& puzzle, GraphicsChoice& graphics)
        : _model(model), _opponent(opponent), _analyst(analyst), _drawJudge(drawJudge), _openings(openings), _editor(editor), _puzzle(puzzle), _graphics(graphics) {}

    MoveList ChessGameControl::LegalMoves() const {
        return _model.IsGameOver() ? MoveList() : MoveGenerator::GenerateLegal(_model.GetPosition());
    }

    std::expected<void, ControlError> ChessGameControl::WriteState(JsonWriter& writer) const {
        writer.BeginObject();
        writer.Key("analysis");
        WriteAnalysis(writer);
        writer.Key("board");
        writer.BeginArray();
        for (int8_t rank = 7; rank >= 0; --rank) {
            String line;
            for (int8_t file = 0; file < 8; ++file) {
                const Piece piece = _model.GetPieceAt(Square{file, rank});
                const char* letters = piece.color == PieceColor::Black ? kBlackLetters : kWhiteLetters;
                line.push_back(letters[static_cast<size_t>(piece.type)]);
            }
            writer.Value(StringView(line));
        }
        writer.EndArray();
        writer.Key("drawOffer");
        WriteDrawOffer(writer);
        writer.Key("editor");
        WriteEditor(writer);
        WriteText(writer, "fen", _model.GetPosition().ToFen());
        writer.Key("graphics");
        writer.BeginObject();
        writer.Member("preset", PresetId(_graphics.Get()));
        writer.EndObject();
        writer.Member("inCheck", _model.IsInCheck());
        writer.Member("legalMoveCount", LegalMoves().size());
        writer.Key("material");
        WriteMaterial(writer, _model.GetPosition());
        writer.Key("moves");
        writer.BeginArray();
        for (const Move move : _model.GetMoves()) {
            writer.Value(StringView(move.ToUci()));
        }
        writer.EndArray();
        writer.Key("opening");
        WriteOpening(writer, _openings, _model);
        writer.Key("opponent");
        WriteOpponent(writer);
        writer.Key("puzzle");
        WritePuzzle(writer);
        writer.Key("review");
        WriteReview(writer);
        writer.Key("sanMoves");
        WriteSanMoves(writer, _model);
        WriteText(writer, "startFen", _model.GetStartFen());
        writer.Member("status", StatusName(_model.GetStatus()));
        writer.Member("takeBackPlies", _puzzle.IsActiveFor(_model) ? 0 : TakeBackPlies(_model.GetMoves().size(), _model.GetTurn(), _opponent.GetPlays()));
        writer.Member("turn", PieceColorId(_model.GetTurn()));
        WriteWinner(writer, _model);
        writer.EndObject();
        return {};
    }

    void ChessGameControl::WriteReview(JsonWriter& writer) const {
        const ReviewState reviewState = _analyst.GetReviewState(_model);
        if (reviewState == ReviewState::Idle) {
            writer.Null();
            return;
        }
        const ReviewProgress progress = _analyst.GetReviewProgress(_model);

        writer.BeginObject();
        writer.Member("depth", kReviewDepth);
        writer.Member("done", progress.done);
        writer.Key("error");
        WriteError(writer, _analyst.GetReviewError());

        writer.Key("moves");
        writer.BeginArray();
        for (const Optional<ReviewedMove>& move : _analyst.GetReviewedMoves(_model)) {
            if (!move) {
                writer.Null();
                continue;
            }
            writer.BeginObject();
            WriteText(writer, "best", move->best.ToUci());
            writer.Member("loss", move->review.loss);
            writer.Member("quality", MoveQualityId(move->review.quality));
            writer.EndObject();
        }
        writer.EndArray();

        writer.Key("positions");
        writer.BeginArray();
        for (const Optional<ReviewedPosition>& position : _analyst.GetReviewedPositions(_model)) {
            if (!position) {
                writer.Null();
                continue;
            }
            const AnalysisScore white = ScoreForWhite(position->score, position->sideToMove);
            writer.BeginObject();
            writer.Key("best");
            if (position->best.IsNull()) {
                writer.Null();
            } else {
                writer.Value(StringView(position->best.ToUci()));
            }
            writer.Key("score");
            WriteScore(writer, white);
            WriteText(writer, "text", ScoreText(white));
            writer.EndObject();
        }
        writer.EndArray();

        writer.Member("state", ReviewStateId(reviewState));
        writer.Member("total", progress.total);
        writer.EndObject();
    }

    void ChessGameControl::WritePuzzle(JsonWriter& writer) const {
        const Puzzle* puzzle = _puzzle.GetCurrent();
        if (puzzle == nullptr || !_puzzle.IsActiveFor(_model)) {
            writer.Null();
            return;
        }
        writer.BeginObject();
        writer.Member("count", _puzzle.GetPuzzles().Size());
        WriteText(writer, "id", puzzle->id);
        writer.Member("index", *_puzzle.GetIndex());
        writer.Member("mistakes", _puzzle.GetMistakes());
        writer.Member("rating", puzzle->rating);
        writer.Member("solver", PieceColorId(puzzle->SolverColor()));
        writer.Member("state", PuzzlePhaseId(_puzzle.GetPhase(_model)));
        writer.Member("step", _puzzle.GetStep(_model));
        writer.Member("steps", _puzzle.GetSteps());
        writer.Key("themes");
        writer.BeginArray();
        for (const String& theme : puzzle->themes) {
            writer.Value(StringView(theme));
        }
        writer.EndArray();
        writer.Member("wrong", _puzzle.WasWrong());
        writer.EndObject();
    }

    void ChessGameControl::WriteEditor(JsonWriter& writer) const {
        if (!_model.IsDrafting()) {
            writer.Null();
            return;
        }
        const auto built = _editor.Build();
        const EditorTool tool = _editor.GetTool();

        writer.BeginObject();
        writer.Key("error");
        if (built) {
            writer.Null();
        } else {
            writer.Value(EditorText::Refusal(built.error()));
        }
        WriteText(writer, "fen", _editor.ToFen());
        writer.Key("tool");
        writer.BeginObject();
        writer.Key("color");
        if (tool.IsEraser()) {
            writer.Null();
        } else {
            writer.Value(PieceColorId(tool.color));
        }
        writer.Member("piece", EditorToolId(tool.type));
        writer.EndObject();
        writer.EndObject();
    }

    void ChessGameControl::WriteOpponent(JsonWriter& writer) const {
        const Optional<PieceColor> plays = _opponent.GetPlays();

        writer.BeginObject();
        writer.Member("engine", OpponentEngineId(_opponent.GetEngine()));
        writer.Key("error");
        WriteError(writer, _opponent.GetError());
        writer.Member("level", GetOpponentLevels()[_opponent.GetLevel()].id);
        writer.Key("plays");
        if (_opponent.IsWatching()) {
            writer.Value(kBothId);
        } else if (plays) {
            writer.Value(PieceColorId(*plays));
        } else {
            writer.Null();
        }
        writer.Key("seats");
        writer.BeginObject();
        for (const PieceColor color : {PieceColor::Black, PieceColor::White}) {  // the keys in byte order
            const SeatChoice& choice = _opponent.GetSeatChoice(color);
            writer.Key(PieceColorId(color));
            writer.BeginObject();
            writer.Member("engine", OpponentEngineId(choice.engine));
            writer.Member("level", GetOpponentLevels()[choice.level].id);
            writer.EndObject();
        }
        writer.EndObject();
        writer.Member("seed", _model.GetSeed());
        writer.Member("thinking", _opponent.IsThinking());
        writer.EndObject();
    }

    void ChessGameControl::WriteDrawOffer(JsonWriter& writer) const {
        const DrawOfferState offer = _drawJudge.GetState(_model);
        if (offer == DrawOfferState::Idle) {
            writer.Null();
            return;
        }
        writer.BeginObject();
        writer.Member("state", offer == DrawOfferState::Pending ? "pending" : "declined");
        writer.EndObject();
    }

    void ChessGameControl::WriteAnalysis(JsonWriter& writer) const {
        writer.BeginObject();
        writer.Key("error");
        WriteError(writer, _analyst.GetError());

        writer.Key("evaluation");
        const Optional<AnalysisLine> evaluation = _analyst.GetEvaluation(_model);
        if (evaluation) {
            const AnalysisScore white = ScoreForWhite(evaluation->score, _model.GetTurn());  // a bar needs one fixed side
            writer.BeginObject();
            writer.Member("depth", evaluation->depth);
            writer.Key("score");
            WriteScore(writer, white);
            WriteText(writer, "text", ScoreText(white));
            writer.EndObject();
        } else {
            writer.Null();
        }
        writer.Member("evaluationOn", _analyst.IsEvaluationOn());

        writer.Key("hint");
        const Optional<AnalysisLine> hint = _analyst.GetHint(_model);
        if (hint) {
            writer.BeginObject();
            writer.Member("depth", hint->depth);
            WriteText(writer, "move", hint->move.ToUci());
            WriteText(writer, "text", ScoreText(hint->score));  // as the engine gave it: the side to move's view
            writer.EndObject();
        } else {
            writer.Null();
        }
        writer.Member("hintRequested", _analyst.IsHintRequested(_model));

        writer.Key("lines");
        writer.BeginArray();
        for (const AnalysisLine& line : _analyst.GetLines(_model)) {
            const AnalysisScore white = ScoreForWhite(line.score, _model.GetTurn());
            writer.BeginObject();
            writer.Member("depth", line.depth);
            WriteText(writer, "move", line.move.ToUci());
            writer.Key("score");
            WriteScore(writer, white);
            WriteText(writer, "text", ScoreText(white));
            writer.EndObject();
        }
        writer.EndArray();

        // One entry per move of "moves", in the same order: null while the move is not rated
        writer.Key("ratings");
        writer.BeginArray();
        for (const Optional<MoveRating>& rating : _analyst.GetRatings(_model)) {
            if (rating) {
                writer.BeginObject();
                writer.Member("brilliant", rating->IsBrilliant());
                writer.Member("reason", MoveRatingReasonId(rating->reason));
                writer.EndObject();
            } else {
                writer.Null();
            }
        }
        writer.EndArray();
        writer.Member("ratingsOn", _analyst.AreRatingsOn());
        writer.Member("thinking", _analyst.IsThinking());
        writer.EndObject();
    }

    std::expected<void, ControlError> ChessGameControl::WriteActions(JsonWriter& writer) const {
        writer.BeginArray();
        for (const Move move : LegalMoves()) {
            writer.BeginObject();
            WriteText(writer, "type", kMoveType);
            WriteText(writer, "uci", move.ToUci());
            writer.EndObject();
        }
        writer.EndArray();
        return {};
    }

    std::expected<void, ControlError> ChessGameControl::PerformAction(JsonView action, JsonWriter& outcome) {
        const auto type = GetParamString(action, "type");
        if (!type || StringView(*type) != kMoveType) {
            return std::unexpected(MakeControlError(ControlErrors::kInvalidParams, "'type' has to be \"move\""));
        }
        const auto uci = GetParamString(action, "uci");
        if (!uci || !IsUciSyntax(StringView(*uci))) {
            return std::unexpected(MakeControlError(ControlErrors::kInvalidParams, "'uci' has to be a move like \"e2e4\" or, for a promotion, \"e7e8q\" (the piece is n, b, r or q)"));
        }
        const StringView spelling(*uci);

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

        outcome.BeginObject();
        outcome.Member("inCheck", _model.IsInCheck());
        WriteText(outcome, "move", move->ToUci());
        outcome.Member("status", StatusName(_model.GetStatus()));
        outcome.Member("turn", PieceColorId(_model.GetTurn()));
        WriteWinner(outcome, _model);
        outcome.EndObject();
        return {};
    }

    std::expected<void, ControlError> ChessGameControl::Reset(JsonView) {
        _model.Reset();
        return {};
    }

    std::expected<void, ControlError> ChessGameControl::SetState(JsonView state) {
        const Optional<JsonView> fenMember = FindParam(state, "fen");
        const Optional<JsonView> opponentMember = FindParam(state, "opponent");
        const Optional<JsonView> analysisMember = FindParam(state, "analysis");
        const Optional<JsonView> takeBackMember = FindParam(state, "takeBack");
        const Optional<JsonView> resignMember = FindParam(state, "resign");
        const Optional<JsonView> drawMember = FindParam(state, "draw");
        const Optional<JsonView> puzzleMember = FindParam(state, "puzzle");
        const Optional<JsonView> reviewMember = FindParam(state, "review");
        const Optional<JsonView> graphicsMember = FindParam(state, "graphics");
        if (!fenMember && !opponentMember && !analysisMember && !takeBackMember && !resignMember && !drawMember && !puzzleMember && !reviewMember && !graphicsMember) {
            return std::unexpected(MakeControlError(ControlErrors::kInvalidParams,
                                                    "'fen' (a FEN string), 'opponent' ({\"plays\", \"level\", \"engine\", \"white\", \"black\", \"seed\"}), both, or "
                                                    "'analysis' ({\"evaluation\", \"hint\", \"ratings\"}), 'takeBack' ({\"plies\"}), 'resign' ({\"color\"}), 'draw' ({}), 'puzzle' ({\"index\"}) or "
                                                    "'review' ({\"start\": true}) or 'graphics' ({\"preset\"}) alone are needed"));
        }
        if (graphicsMember) {
            if (fenMember || opponentMember || analysisMember || takeBackMember || resignMember || drawMember || puzzleMember || reviewMember) {
                return std::unexpected(
                    MakeControlError(ControlErrors::kInvalidParams, "'graphics' cannot be combined with another member: the look is the person's, not part of the game they change"));
            }
            return SetGraphics(*graphicsMember);
        }
        if (reviewMember) {
            if (fenMember || opponentMember || analysisMember || takeBackMember || resignMember || drawMember || puzzleMember) {
                return std::unexpected(MakeControlError(ControlErrors::kInvalidParams, "'review' cannot be combined with another member: it belongs to the finished game they would change"));
            }
            return SetReview(*reviewMember);
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
            const auto fen = GetParamString(state, "fen");
            if (!fen) {
                return std::unexpected(MakeControlError(ControlErrors::kInvalidParams, "'fen' has to be a FEN string"));
            }
            const auto parsed = Position::FromFen(StringView(*fen));
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

    std::expected<void, ControlError> ChessGameControl::SetAnalysis(JsonView analysis) {
        // Check everything first: a refused call changes nothing, the switch of the evaluation included
        const auto pick = ParseAnalysis(analysis);
        if (!pick) {
            return std::unexpected(pick.error());
        }
        if (pick->hint.value_or(false)) {
            if (const Optional<HintRefusal> refusal = HintRefusalFor(_model, HintSeatsFor(_model, _puzzle, _opponent.GetSides()))) {
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

    std::expected<void, ControlError> ChessGameControl::SetTakeBack(JsonView takeBack) {
        if (_puzzle.IsActiveFor(_model)) {
            return std::unexpected(MakeControlError(ControlErrors::kInvalidParams, kPuzzleOnUsage));
        }
        if (!takeBack.IsObject()) {
            return std::unexpected(MakeControlError(ControlErrors::kInvalidParams, kTakeBackUsage));
        }
        for (const JsonMember member : takeBack.Members()) {
            if (!member.key.TextEquals("plies")) {
                return std::unexpected(MakeControlError(ControlErrors::kInvalidParams, kTakeBackUsage));
            }
        }
        const size_t played = _model.GetMoves().size();
        size_t plies = TakeBackPlies(played, _model.GetTurn(), _opponent.GetPlays());
        if (FindParam(takeBack, "plies")) {
            const Optional<int64_t> asked = GetParamInt(takeBack, "plies");
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

    std::expected<void, ControlError> ChessGameControl::SetResign(JsonView resign) {
        if (_puzzle.IsActiveFor(_model)) {
            return std::unexpected(MakeControlError(ControlErrors::kInvalidParams, kPuzzleOnUsage));
        }
        if (!resign.IsObject()) {
            return std::unexpected(MakeControlError(ControlErrors::kInvalidParams, kResignUsage));
        }
        for (const JsonMember member : resign.Members()) {
            if (!member.key.TextEquals("color")) {
                return std::unexpected(MakeControlError(ControlErrors::kInvalidParams, kResignUsage));
            }
        }
        Optional<PieceColor> named;
        if (FindParam(resign, "color")) {
            const auto text = GetParamString(resign, "color");
            named = text ? ParseColor(StringView(*text)) : std::nullopt;
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

    std::expected<void, ControlError> ChessGameControl::SetDraw(JsonView draw) {
        if (_puzzle.IsActiveFor(_model)) {
            return std::unexpected(MakeControlError(ControlErrors::kInvalidParams, kPuzzleOnUsage));
        }
        if (!draw.IsObject() || draw.Size() != 0) {
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

    std::expected<void, ControlError> ChessGameControl::SetPuzzle(JsonView puzzle) {
        if (!puzzle.IsObject() || puzzle.Size() != 1) {
            return std::unexpected(MakeControlError(ControlErrors::kInvalidParams, kPuzzleUsage));
        }
        const JsonMember member = *puzzle.Members().begin();
        String key;
        member.key.ReadText(key);
        const PuzzleSet& puzzles = _puzzle.GetPuzzles();
        if (key == "index" || key == "id") {
            Optional<size_t> index;
            if (key == "index") {
                const Optional<int64_t> asked = GetParamInt(puzzle, "index");
                if (!asked || *asked < 0 || static_cast<uint64_t>(*asked) >= puzzles.Size()) {
                    return std::unexpected(MakeControlError(ControlErrors::kInvalidParams, Format("'index' has to be an integer below the {} puzzles of the set", puzzles.Size())));
                }
                index = static_cast<size_t>(*asked);
            } else {
                const auto id = GetParamString(puzzle, "id");
                index = id ? puzzles.FindIndexById(StringView(*id)) : std::nullopt;
                if (!index) {
                    return std::unexpected(MakeControlError(ControlErrors::kInvalidParams, "'id' has to be the id of a puzzle of the set"));
                }
            }
            const bool begun = _puzzle.Begin(_model, *index);
            EXPECT(begun, "ChessGameControl: a puzzle of the set does not begin");
            return {};
        }
        if ((key != "next" && key != "retry" && key != "end") || member.value.AsBool() != true) {
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

    std::expected<void, ControlError> ChessGameControl::SetReview(JsonView review) {
        if (!review.IsObject() || review.Size() != 1) {
            return std::unexpected(MakeControlError(ControlErrors::kInvalidParams, kReviewUsage));
        }
        const JsonMember member = *review.Members().begin();
        const bool isStart = member.key.TextEquals("start");
        const bool isCancel = member.key.TextEquals("cancel");
        if ((!isStart && !isCancel) || member.value.AsBool() != true) {
            return std::unexpected(MakeControlError(ControlErrors::kInvalidParams, kReviewUsage));
        }
        if (isCancel) {
            _analyst.AbandonReview();
            return {};
        }
        if (_puzzle.IsActiveFor(_model)) {
            return std::unexpected(MakeControlError(ControlErrors::kInvalidParams, "a puzzle is on: leave it first with {\"puzzle\": {\"end\": true}} (a puzzle is not reviewed)"));
        }
        const std::expected<void, ReviewRefusal> started = _analyst.StartReview(_model);
        if (started) {
            return {};
        }
        switch (started.error()) {
            case ReviewRefusal::NotFinished:
                return std::unexpected(MakeControlError(ControlErrors::kInvalidParams, "the game is not finished: only a finished game is reviewed"));
            case ReviewRefusal::NoMoves:
                return std::unexpected(MakeControlError(ControlErrors::kInvalidParams, "the game has no moves: there is nothing to review"));
        }
        return std::unexpected(MakeControlError(ControlErrors::kInvalidParams, kReviewUsage));
    }

    std::expected<void, ControlError> ChessGameControl::SetGraphics(JsonView graphics) {
        if (!graphics.IsObject() || graphics.Size() != 1 || !FindParam(graphics, "preset")) {
            return std::unexpected(MakeControlError(ControlErrors::kInvalidParams, kGraphicsUsage));
        }
        const auto id = GetParamString(graphics, "preset");
        const Optional<GraphicsPreset> preset = id ? ParsePreset(StringView(*id)) : std::nullopt;
        if (!preset) {
            return std::unexpected(MakeControlError(ControlErrors::kInvalidParams, kGraphicsUsage));
        }
        _graphics.Set(*preset);
        return {};
    }

    uint64_t ChessGameControl::GetStateVersion() const {
        return _model.GetVersion() + _opponent.GetRevision() + _analyst.GetRevision() + _drawJudge.GetRevision() + _puzzle.GetRevision() + _editor.GetRevision() + _graphics.GetRevision();
    }

}  // namespace BECore::Chess
