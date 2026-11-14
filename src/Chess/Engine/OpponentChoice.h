#pragma once

#include <Chess/Engine/OpponentController.h>

namespace BECore::Chess {

    /// Whether the program plays: two people on one screen, or a person against the computer. The first choice of the opponent panel
    CORE_ENUM(OpponentMode, uint8_t, Two, Computer)

    /// The name on the wire of a color, as the control API spells it: "white", "black"
    [[nodiscard]] eastl::string_view PieceColorId(PieceColor color);

    /// The id of a mode: "two", "computer"
    [[nodiscard]] eastl::string_view OpponentModeId(OpponentMode mode);

    /// The mode with this id (exactly as written: ids are lower case), or nothing
    [[nodiscard]] Optional<OpponentMode> ParseMode(eastl::string_view id);

    /// The color with this id ("white" or "black", exactly as written), or nothing
    [[nodiscard]] Optional<PieceColor> ParseColor(eastl::string_view id);

    /// BadId: the id is not one of the table; nothing was changed and no engine was asked. Engine: the controller refused the pick, see ChoiceError::engine
    CORE_ENUM(ChoiceErrorKind, uint8_t, BadId, Engine)

    /// Why a pick of OpponentChoice did not happen as asked
    struct ChoiceError {
        ChoiceErrorKind kind = ChoiceErrorKind::BadId;
        eastl::string detail;  ///< a sentence for the log: for BadId what was wrong, for Engine the detail of the engine error
        EngineError engine;    ///< what OpponentController said; meaningful for the kind Engine only
    };

    /**
     * @brief The choice of the opponent as a person makes it: a mode, a color, an engine and a level, each picked by an id.
     *
     * OpponentController knows one thing about the sides, the color the PROGRAM plays (GetPlays(), nothing for two people), and the control API says the same.
     * The opponent panel speaks of the PERSON: the mode (Two players / Computer) and the color the person plays (White / Black), and the program plays the other one.
     * The person's color has to be remembered while the mode is Two players, when the controller holds no side at all; this class remembers it and turns between the two views.
     *
     * - SetMode("computer") makes the program play the opposite of Color(); SetMode("two") makes nobody play and keeps the color.
     * - SetColor() remembers the color and, when the program plays, remakes the opponent for the opposite one.
     * - SetEngine() and SetLevel() go to the controller as they are (it remembers both while nobody plays).
     *
     * An id that is not in the table (empty, other case, a prefix, a trailing space) is a ChoiceError of the kind BadId: nothing changes, no engine is asked, the revision of
     * the controller stays. A refused pick (no Stockfish found) is a ChoiceError of the kind Engine; the controller keeps the error and nothing else changes, so
     * Mode(), Color(), the engine and the level say what they said before the pick. Nothing falls back to another engine.
     *
     * Color() follows the controller: when the program plays, the person's color is the opposite one, whoever set it (the control API's set_state included), and that color is
     * remembered, so a later "nobody" keeps it. It is refreshed whenever Color() is read, so a display that reads it every frame never loses it.
     *
     * Holds a reference to the controller, which must outlive it. No scene, no component, main thread only.
     */
    class OpponentChoice {
    public:
        explicit OpponentChoice(OpponentController& opponent) : _opponent(opponent) {}

        OpponentChoice(const OpponentChoice&) = delete;
        OpponentChoice& operator=(const OpponentChoice&) = delete;
        OpponentChoice(OpponentChoice&&) = delete;
        OpponentChoice& operator=(OpponentChoice&&) = delete;

        /// Forgets the remembered color: White again, as at the start of a game. The controller is not touched
        void Reset() noexcept {
            _personColor = PieceColor::White;
        }

        /// "two" or "computer"
        std::expected<void, ChoiceError> SetMode(eastl::string_view id);

        /// The color the person plays, "white" or "black"
        std::expected<void, ChoiceError> SetColor(eastl::string_view id);

        /// An engine id of the control API, "stockfish" or "own"
        std::expected<void, ChoiceError> SetEngine(eastl::string_view id);

        /// A level id of the control API, "easy", "medium", "hard" or "max"
        std::expected<void, ChoiceError> SetLevel(eastl::string_view id);

        /// Computer while the program plays, else Two (also while an error stands that the program's side did not leave)
        [[nodiscard]] OpponentMode Mode() const noexcept;

        /// The color the person plays: the opposite of the program's when it plays (and then remembered), else the one remembered; White at the start
        [[nodiscard]] PieceColor Color() const noexcept;

        /// The id of Mode()
        [[nodiscard]] eastl::string_view ModeId() const noexcept;

        /// The id of Color()
        [[nodiscard]] eastl::string_view ColorId() const noexcept;

        /// The id of the picked engine, as in the control API
        [[nodiscard]] eastl::string_view EngineId() const noexcept;

        /// The id of the picked level, as in the control API
        [[nodiscard]] eastl::string_view LevelId() const noexcept;

    private:
        OpponentController& _opponent;
        mutable PieceColor _personColor = PieceColor::White;  ///< the last color seen for the person; Color() refreshes it from the controller
    };

}  // namespace BECore::Chess
