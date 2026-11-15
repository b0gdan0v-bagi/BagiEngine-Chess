#pragma once

#include <Chess/Engine/OpponentController.h>

namespace BECore::Chess {

    /// Whether the program plays: two people on one screen, or a person against the computer. The first choice of the opponent panel
    CORE_ENUM(OpponentMode, uint8_t, Two, Computer)

    /// The id of a color as the control API spells it: "white", "black"
    [[nodiscard]] StringView PieceColorId(PieceColor color);

    [[nodiscard]] StringView OpponentModeId(OpponentMode mode);

    /// The mode with this id (exactly as written: ids are lower case), or nothing when it is not in the table
    [[nodiscard]] Optional<OpponentMode> ParseMode(StringView id);

    /// The color with this id (exactly as written: ids are lower case), or nothing when it is not in the table
    [[nodiscard]] Optional<PieceColor> ParseColor(StringView id);

    /// BadId: the id is not in the table; nothing changed and no engine was asked. Engine: the controller refused the pick (ChoiceError::engine)
    CORE_ENUM(ChoiceErrorKind, uint8_t, BadId, Engine)

    /// Why a pick of OpponentChoice did not happen as asked
    struct ChoiceError {
        ChoiceErrorKind kind = ChoiceErrorKind::BadId;
        String detail;       ///< a sentence for the log
        EngineError engine;  ///< what OpponentController said; meaningful for the kind Engine only
    };

    /**
     * @brief The choice of the opponent as a person makes it: a mode, a color, an engine and a level, each picked by an id.
     *
     * The controller and the control API speak of the color the PROGRAM plays (nothing for two people); the opponent panel speaks of the PERSON: a mode (Two players / Computer)
     * and the person's color, the program playing the other one. The person's color has to be remembered while nobody plays; this class remembers it and turns between the two views.
     * SetMode("computer") makes the program play the opposite of Color(); SetMode("two") makes nobody play and keeps the color.
     *
     * An id that is not in the table (empty, other case, a prefix, a trailing space) is a ChoiceError of the kind BadId: nothing changes, no engine is asked. A refused pick is a
     * ChoiceError of the kind Engine: the controller keeps the error and nothing else changes, so Mode(), Color(), the engine and the level say what they said before the pick.
     *
     * Color() follows the controller: when the program plays, the person's color is the opposite one, whoever set it (the control API's set_state included), and that color is
     * remembered, so a later "nobody" keeps it. It is refreshed whenever Color() is read, so a display that reads it every frame never loses it.
     *
     * Holds a reference to the controller, which must outlive it. Main thread only.
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

        std::expected<void, ChoiceError> SetMode(StringView id);

        /// The person's color. While the program plays it moves to the opposite side (a refused pick keeps the old color); otherwise (nobody plays, or two programs: GetPlays() is nothing) only the
        /// remembered color changes
        std::expected<void, ChoiceError> SetColor(StringView id);

        /// An engine id of the control API ("stockfish", "own")
        std::expected<void, ChoiceError> SetEngine(StringView id);

        /// A level id of the control API ("easy" to "max")
        std::expected<void, ChoiceError> SetLevel(StringView id);

        /// Computer while the program plays one side, else Two
        [[nodiscard]] OpponentMode Mode() const noexcept;

        /// The color the person plays: the opposite of the program's when it plays (and then remembered), else the one remembered
        [[nodiscard]] PieceColor Color() const noexcept;

        [[nodiscard]] StringView ModeId() const noexcept;

        [[nodiscard]] StringView ColorId() const noexcept;

        [[nodiscard]] StringView EngineId() const noexcept;

        [[nodiscard]] StringView LevelId() const noexcept;

    private:
        OpponentController& _opponent;
        mutable PieceColor _personColor = PieceColor::White;  ///< Color() refreshes it from the controller
    };

}  // namespace BECore::Chess
