#pragma once

#include <Chess/ChessGameControl.h>
#include <Chess/Engine/DrawOfferJudge.h>
#include <Chess/Engine/OpponentChoice.h>
#include <Chess/Engine/OpponentController.h>
#include <Chess/Engine/PositionAnalyst.h>
#include <Chess/Game/ChessGameModel.h>
#include <Chess/Game/OpeningBook.h>
#include <Chess/Game/PositionEditor.h>
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
     * Owns the PositionEditor, the draft of a position the person builds on the board. The game is edited without being changed: BeginEditing() has the model show the draft
     * (ChessGameModel::ShowDraft(), IsEditing() is its IsDrafting()), CancelEditing() puts the game back as it was, and PlayFromHere() starts a new game from the draft through
     * ChessGameModel::SetPosition(), so the opponent, the analyst and the keeper see an ordinary game from a FEN. Whatever else changes the game (Reset(), a control client's set_state, a loaded save)
     * ends the draft by the model's own rule.
     *
     * Start() loads the opening names (OpeningBook) from kOpeningsPath; a file that is missing or cannot be read leaves the book empty (one warning), so the game plays and no position has a name.
     *
     * IGame has no per-frame hook, so ChessBoardComponent runs GetOpponent().Update(), GetAnalyst().Update(), GetDrawJudge().Update() and UpdateKeeper() every frame. Start() ends by restoring
     * the saved game, the opponent choice and the player's audio levels (ChessSaveKeeper, when the run asks for it). Stop() starts by writing them, then destroys the opponent and shuts the analyst
     * and the judge down, which happens before the task system goes down (Application::Cleanup()): a search that nobody cancelled would hold the join of the pool.
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

        /// The color the person resigns for in the game as it stands (ResigningColor()), or nothing when nobody can: a finished game, two programs. The one rule of both
        /// Resign buttons, the HUD's and the menu section's
        [[nodiscard]] Optional<PieceColor> ResigningNow() const;

        /// The virtual path of the table of opening names that Start() loads
        static constexpr StringView kOpeningsPath = "assets/openings/openings.tsv";

        /// The book of the table at @p virtualPath; empty, with one warning, when the file is missing or cannot be read (the game goes on without names)
        [[nodiscard]] static OpeningBook LoadOpeningBook(StringView virtualPath);

        const OpeningBook& GetOpenings() const noexcept {
            return _openings;
        }

        /// Replaces the book Start() loaded; for a test, which has no game content root to load it from
        void SetOpenings(OpeningBook openings) {
            _openings = eastl::move(openings);
        }

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

        /// The reason the last PlayFromHere() refused, until the next change of the draft; empty when not editing
        [[nodiscard]] StringView GetEditorMessage() const noexcept {
            return IsEditing() ? StringView(_editorMessage) : StringView();
        }

        /// The active game, or nullptr when no game is active or the active game is not Chess (the same for the GetActive*() below)
        static ChessGame* GetActive();

        /// The model of the active game
        static ChessGameModel* GetActiveModel();

        /// The opponent of the active game
        static OpponentController* GetActiveOpponent();

        /// The choice of the opponent of the active game
        static OpponentChoice* GetActiveChoice();

        /// The analyst of the active game
        static PositionAnalyst* GetActiveAnalyst();

        /// The judge of draw offers of the active game
        static DrawOfferJudge* GetActiveDrawJudge();

        /// The opening names of the active game
        static const OpeningBook* GetActiveOpenings();

        /// The position editor of the active game
        static PositionEditor* GetActiveEditor();

    private:
        ChessGameModel _model;
        OpponentController _opponent;  ///< after the model (it holds a reference to it) and before the adapter
        OpponentChoice _choice;        ///< after the controller (it holds a reference to it)
        PositionAnalyst _analyst;      ///< after the controller (its factory reads the controller's settings) and before the adapter
        DrawOfferJudge _drawJudge;     ///< after the analyst (its factory asks it) and before the adapter
        OpeningBook _openings;         ///< before the adapter, which reads it
        PositionEditor _editor;
        String _editorMessage;
        ChessSaveKeeper _keeper;  ///< after everything it reads (it is handed the model, the controller and the choice in each call, and destroyed first)
        ChessGameControl _control;
        bool _stopped = false;  ///< between Stop() and the next Start(): UpdateKeeper() writes nothing
    };

}  // namespace BECore::Chess
