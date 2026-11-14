#include "OpponentChoice.h"

namespace BECore::Chess {

    namespace {

        constexpr StringView kWhiteId = "white";
        constexpr StringView kBlackId = "black";

        ChoiceError BadId(StringView what, StringView id) {
            return ChoiceError{ChoiceErrorKind::BadId, Format("'{}' is not a {}", id, what), {}};
        }

        std::expected<void, ChoiceError> FromController(std::expected<void, EngineError> picked) {
            if (picked) {
                return {};
            }
            EngineError engine = std::move(picked.error());
            return std::unexpected(ChoiceError{ChoiceErrorKind::Engine, engine.detail, std::move(engine)});
        }

    }  // namespace

    StringView PieceColorId(PieceColor color) {
        return color == PieceColor::White ? kWhiteId : kBlackId;
    }

    StringView OpponentModeId(OpponentMode mode) {
        switch (mode) {
            case OpponentMode::Two:
                return "two";
            case OpponentMode::Computer:
                return "computer";
        }
        return "";
    }

    Optional<OpponentMode> ParseMode(StringView id) {
        for (const OpponentMode mode : EnumUtils<OpponentMode>::Values()) {
            if (OpponentModeId(mode) == id) {
                return mode;
            }
        }
        return std::nullopt;
    }

    Optional<PieceColor> ParseColor(StringView id) {
        if (id == kWhiteId) {
            return PieceColor::White;
        }
        if (id == kBlackId) {
            return PieceColor::Black;
        }
        return std::nullopt;
    }

    std::expected<void, ChoiceError> OpponentChoice::SetMode(StringView id) {
        const Optional<OpponentMode> mode = ParseMode(id);
        if (!mode) {
            return std::unexpected(BadId("mode", id));
        }
        const PieceColor person = Color();  // read first: it remembers the color of the side the program is about to leave
        if (*mode == OpponentMode::Two) {
            return FromController(_opponent.SetSide(std::nullopt));
        }
        return FromController(_opponent.SetSide(Opposite(person)));
    }

    std::expected<void, ChoiceError> OpponentChoice::SetColor(StringView id) {
        const Optional<PieceColor> color = ParseColor(id);
        if (!color) {
            return std::unexpected(BadId("color", id));
        }
        if (!_opponent.GetPlays()) {
            _personColor = *color;
            return {};
        }
        std::expected<void, ChoiceError> picked = FromController(_opponent.SetSide(Opposite(*color)));
        if (picked) {
            _personColor = *color;  // a refused pick keeps the color the program's side says
        }
        return picked;
    }

    std::expected<void, ChoiceError> OpponentChoice::SetEngine(StringView id) {
        const Optional<OpponentEngine> engine = FindOpponentEngine(id);
        if (!engine) {
            return std::unexpected(BadId("engine", id));
        }
        return FromController(_opponent.SetEngine(*engine));
    }

    std::expected<void, ChoiceError> OpponentChoice::SetLevel(StringView id) {
        const Optional<size_t> level = FindOpponentLevel(id);
        if (!level) {
            return std::unexpected(BadId("level", id));
        }
        return FromController(_opponent.SetLevel(*level));
    }

    OpponentMode OpponentChoice::Mode() const noexcept {
        return _opponent.GetPlays() ? OpponentMode::Computer : OpponentMode::Two;
    }

    PieceColor OpponentChoice::Color() const noexcept {
        if (const Optional<PieceColor> plays = _opponent.GetPlays()) {
            _personColor = Opposite(*plays);
        }
        return _personColor;
    }

    StringView OpponentChoice::ModeId() const noexcept {
        return OpponentModeId(Mode());
    }

    StringView OpponentChoice::ColorId() const noexcept {
        return PieceColorId(Color());
    }

    StringView OpponentChoice::EngineId() const noexcept {
        return OpponentEngineId(_opponent.GetEngine());
    }

    StringView OpponentChoice::LevelId() const noexcept {
        return GetOpponentLevels()[_opponent.GetLevel()].id;
    }

}  // namespace BECore::Chess
