#pragma once

#include <Chess/ChessGameControl.h>
#include <Chess/Engine/DrawOfferJudge.h>
#include <Chess/Engine/OpponentChoice.h>
#include <Chess/Engine/OpponentController.h>
#include <Chess/Engine/PositionAnalyst.h>
#include <Chess/Game/ChessGameModel.h>
#include <Chess/Game/OpeningBook.h>
#include <Chess/Game/PositionEditor.h>
#include <Chess/Game/PuzzleSession.h>
#include <Chess/Graphics/GraphicsChoice.h>
#include <Chess/Save/ChessSaveKeeper.h>
#include <Games/IGame.h>

namespace BECore::Chess {

    /// Where BeginEditing() takes the draft from: the game's position, an empty board or the opening position.
    CORE_ENUM(EditStart, uint8_t, Current, Empty, Start)

    /**
     * @brief Concrete IGame for chess; its scene is "ChessScene".
     *
     * Owns the model the scene shares (components reach it, the opponent, the choice and the analyst through the GetActive*() calls), the computer opponent (OpponentController), the choice the
     * opponent panel makes over it (OpponentChoice: it remembers the person's color while nobody plays), the analysis of the position (PositionAnalyst, with the default factory over the settings the
     * controller holds), the judge of the person's draw offers (DrawOfferJudge, which makes its engine through the analyst's factory, so a factory given to the analyst reaches it too), the
     * save keeper and the adapter of the control API (ChessGameControl). A game starts with nobody as the opponent (it is made when a side is picked) and the analysis off.
     *
     * Owns the GraphicsChoice, the look the person picked: Start() sets Classic (the save, when the run has one, then restores the person's pick), Reset() leaves it alone.
     *
     * Owns the PositionEditor, the draft of a position the person builds on the board. The game is edited without being changed: BeginEditing() has the model show the draft
     * (ChessGameModel::ShowDraft(), IsEditing() is its IsDrafting()), CancelEditing() puts the game back as it was, and PlayFromHere() starts a new game from the draft through
     * ChessGameModel::SetPosition(), so the opponent, the analyst and the keeper see an ordinary game from a FEN. Whatever else changes the game (Reset(), a control client's set_state, a loaded save)
     * ends the draft by the model's own rule.
     *
     * Start() loads the opening names (OpeningBook) from kOpeningsPath; a file that is missing or cannot be read leaves the book empty (one warning), so the game plays and no position has a name.
     * It loads the puzzles (PuzzleSet) from kPuzzlesPath the same way: no file, no puzzle.
     *
     * Owns the PuzzleSession, the judge of a puzzle. A puzzle is **on** (IsPuzzleOn()) exactly while the model's game is the puzzle's line (PuzzleSession::IsActiveFor()), so a Reset(), a
     * set_state or a takeback under the setup ends it with no flag to clear. While one is on the board hands the opponent no frame (a search that was in flight is never played: after the puzzle
     * the game has moved on and the controller drops it as stale), the person moves for the solver's color whatever the opponent is picked, the board is turned for that color (BoardBottomColor()),
     * and takeback, resignation, draw offers and the save are off. The opponent's pick is left as it was; LeavePuzzle() is a plain Reset().
     *
     * IGame has no per-frame hook, so ChessBoardComponent runs GetOpponent().Update(), GetAnalyst().Update(), GetDrawJudge().Update() and UpdateKeeper() every frame. Start() ends by restoring
     * the saved game, the opponent choice, the player's audio levels and the look (ChessSaveKeeper, when the run asks for it). Stop() starts by writing them, then destroys the opponent and shuts the
     * analyst and the judge down, which happens before the task system goes down (Application::Cleanup()): a search that nobody cancelled would hold the join of the pool.
     */
    class ChessGame : public IGame {
        BE_CLASS(ChessGame)
    public:
        ChessGame();
        ~ChessGame() override = default;

        PoolString GetName() const override;
        PoolString GetSceneName() const override;

        void Start() override;
        void Stop() override;
        void Reset() override;

        IControllableGame* AsControllable() override;

        ChessGameModel& GetModel() noexcept {
            return _model;
        }
        const ChessGameModel& GetModel() const noexcept {
            return _model;
        }

        OpponentController& GetOpponent() noexcept {
            return _opponent;
        }
        const OpponentController& GetOpponent() const noexcept {
            return _opponent;
        }

        OpponentChoice& GetChoice() noexcept {
            return _choice;
        }
        const OpponentChoice& GetChoice() const noexcept {
            return _choice;
        }

        /// The look the person picked (the pieces' sprites and the board theme); no part of a game, so Reset() leaves it alone
        GraphicsChoice& GetGraphics() noexcept {
            return _graphics;
        }
        const GraphicsChoice& GetGraphics() const noexcept {
            return _graphics;
        }

        PositionAnalyst& GetAnalyst() noexcept {
            return _analyst;
        }
        const PositionAnalyst& GetAnalyst() const noexcept {
            return _analyst;
        }

        DrawOfferJudge& GetDrawJudge() noexcept {
            return _drawJudge;
        }
        const DrawOfferJudge& GetDrawJudge() const noexcept {
            return _drawJudge;
        }

        /// The person offers a draw, for the menu's button (PersonOffersDraw(): two people agree at once, against the program the judge's verdict comes with the frames of
        /// ChessBoardComponent::OnUpdate); the control adapter makes the same offer through the same function
        std::expected<void, DrawOfferRefusal> OfferDraw();

        /// Whether a draw may be offered now: the same refusals as OfferDraw() with no side effect (the menu's button greys on it)
        [[nodiscard]] bool CanOfferDraw() const;

        /// The color the person resigns for in the game as it stands (ResigningColor()), or nothing when nobody can: a finished game, two programs. The one rule of both
        /// Resign buttons, the HUD's and the menu section's
        [[nodiscard]] Optional<PieceColor> ResigningNow() const;

        /// Whether a review of the game may begin now: no puzzle on, a finished game with moves (ReviewRefusalFor()) and no review that runs or is done (a failed one may be retried).
        /// The one rule of the Review section's Start and of the result bar's Review button
        [[nodiscard]] bool ReviewStartAllowedNow() const;

        /// The virtual path of the table of opening names that Start() loads
        static constexpr StringView kOpeningsPath = "assets/openings/openings.tsv";

        /// The book of the table at @p virtualPath; empty, with one warning, when the file is missing or cannot be read (the game goes on without names)
        [[nodiscard]] static OpeningBook LoadOpeningBook(StringView virtualPath);

        /// The virtual path of the table of puzzles that Start() loads
        static constexpr StringView kPuzzlesPath = "assets/puzzles/puzzles.tsv";

        /// The puzzles of the table at @p virtualPath; empty, with one warning, when the file is missing or cannot be read (the game plays without puzzles)
        [[nodiscard]] static PuzzleSet LoadPuzzles(StringView virtualPath);

        const OpeningBook& GetOpenings() const noexcept {
            return _openings;
        }

        /// Replaces the book Start() loaded; for a test, which has no game content root to load it from
        void SetOpenings(OpeningBook openings) {
            _openings = eastl::move(openings);
        }

        PuzzleSession& GetPuzzle() noexcept {
            return _puzzle;
        }
        const PuzzleSession& GetPuzzle() const noexcept {
            return _puzzle;
        }

        /// Replaces the puzzles Start() loaded and forgets the one picked; for a test, which has no game content root to load them from
        void SetPuzzles(PuzzleSet puzzles) {
            _puzzle.SetPuzzles(eastl::move(puzzles));
        }

        /// A puzzle is on: the game is its line (PuzzleSession::IsActiveFor()). Judged from the model at every call
        [[nodiscard]] bool IsPuzzleOn() const {
            return _puzzle.IsActiveFor(_model);
        }

        /// Starts puzzle @p index (a new game from its position: the model's SetPosition()); false, nothing changed, for an index outside the set
        bool BeginPuzzle(size_t index);

        /// The puzzle after the one that is on, the first after the last; the first one when none is on. False for an empty set
        bool NextPuzzle();

        /// Begins the puzzle that is on again; false when none is on
        bool RetryPuzzle();

        /// Ends the puzzle with a plain Reset() (a new game, the opponent as picked); false and nothing changed when none is on
        bool LeavePuzzle();

        /// Once per frame from ChessBoardComponent::OnUpdate (never from a draw): the judge's verdict and the program's reply
        void UpdatePuzzle(float deltaSeconds);

        /// The color at the bottom of the board: the solver's while a puzzle is on, else BottomColor() of what the program plays. The one place the board and the material lists ask
        [[nodiscard]] PieceColor BoardBottomColor() const;

        /// The side to move is the program's for the click flow of the board: with a puzzle on every turn but the solver's (the program's reply, which the person must not play), else the
        /// opponent's seat
        [[nodiscard]] bool ProgramToMoveNow() const;

        const ChessSaveKeeper& GetKeeper() const noexcept {
            return _keeper;
        }

        /// Once per frame from ChessBoardComponent::OnUpdate (never from a draw): writes the game when it changed and the debounce of ChessSaveKeeper has passed. Does nothing in a run that saves
        /// nothing
        void UpdateKeeper();

        PositionEditor& GetEditor() noexcept {
            return _editor;
        }
        const PositionEditor& GetEditor() const noexcept {
            return _editor;
        }

        /// A draft is shown (ChessGameModel::IsDrafting()); the editor's state means something only then
        [[nodiscard]] bool IsEditing() const noexcept {
            return _model.IsDrafting();
        }

        /// Starts a draft from @p start (Current: the game's position, a finished game and a viewed one too) and shows it; a draft that is open is begun again. Drops the message.
        void BeginEditing(EditStart start);

        /// To call after every change of the editor by a person's action (never per frame): shows the draft again and drops the message. Does nothing while not editing.
        void EditChanged();

        /**
         * @brief Starts a new game from the draft (Build(), then ChessGameModel::SetPosition()), which ends the draft.
         *
         * @return false when not editing, or when the rules refuse the draft (FromFen's): the draft stays and GetEditorMessage() says why.
         */
        bool PlayFromHere();

        /// Ends the draft and leaves the game exactly as it was (position, moves, status and version)
        void CancelEditing();

        /**
         * @brief Loads the FEN a person pasted (surrounding whitespace dropped) as the draft, beginning the editor when none is open.
         *
         * @return false when the text is no position the rules take: nothing changes (an editor is not even begun) and GetEditorMessage() says why.
         */
        bool PasteFen(StringView text);

        /// What the last PlayFromHere() or PasteFen() refused, or the clipboard said, until the next change of the draft; also while no editor is open (the section "Position" shows it then)
        [[nodiscard]] StringView GetEditorMessage() const noexcept {
            return _editorMessage;
        }

        /// Replaces the message; an empty one takes it away. The words are EditorText's
        void SetEditorMessage(StringView message);

        /// The active game, or nullptr when no game is active or the active game is not Chess (the same for the GetActive*() below)
        static ChessGame* GetActive();

        /// The model of the active game
        static ChessGameModel* GetActiveModel();

        /// The opponent of the active game
        static OpponentController* GetActiveOpponent();

        /// The choice of the opponent of the active game
        static OpponentChoice* GetActiveChoice();

        /// The graphics choice of the active game
        static GraphicsChoice* GetActiveGraphics();

        /// The analyst of the active game
        static PositionAnalyst* GetActiveAnalyst();

        /// The judge of draw offers of the active game
        static DrawOfferJudge* GetActiveDrawJudge();

        /// The opening names of the active game
        static const OpeningBook* GetActiveOpenings();

        /// The position editor of the active game
        static PositionEditor* GetActiveEditor();

        /// The puzzle judge of the active game
        static PuzzleSession* GetActivePuzzle();

    private:
        ChessGameModel _model;
        OpponentController _opponent;  ///< after the model (it holds a reference to it) and before the adapter
        OpponentChoice _choice;        ///< after the controller (it holds a reference to it)
        GraphicsChoice _graphics;      ///< the person's look, not the game's: before the adapter and the keeper
        PositionAnalyst _analyst;      ///< after the controller (its factory reads the controller's settings) and before the adapter
        DrawOfferJudge _drawJudge;     ///< after the analyst (its factory asks it) and before the adapter
        OpeningBook _openings;         ///< before the adapter, which reads it
        PuzzleSession _puzzle;         ///< after the model (it judges it) and before the adapter and the keeper
        PositionEditor _editor;
        String _editorMessage;
        ChessSaveKeeper _keeper;  ///< after everything it reads (it is handed the model, the controller and the choice in each call and keeps the graphics choice from Start(); destroyed first)
        ChessGameControl _control;
        bool _stopped = false;  ///< between Stop() and the next Start(): UpdateKeeper() writes nothing
    };

}  // namespace BECore::Chess
