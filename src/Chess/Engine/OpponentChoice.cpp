#include "OpponentChoice.h"

namespace BECore::Chess {

    namespace {

        constexpr eastl::string_view kWhiteId = "white";
        constexpr eastl::string_view kBlackId = "black";

        ChoiceError BadId(eastl::string_view what, eastl::string_view id) {
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

    eastl::string_view PieceColorId(PieceColor color) {
        return color == PieceColor::White ? kWhiteId : kBlackId;
    }

    eastl::string_view OpponentModeId(OpponentMode mode) {
        switch (mode) {
            case OpponentMode::Two:
                return "two";
            case OpponentMode::Computer:
                return "computer";
        }
        return "";
    }

    Optional<OpponentMode> ParseMode(eastl::string_view id) {
        for (const OpponentMode mode : EnumUtils<OpponentMode>::Values()) {
            if (OpponentModeId(mode) == id) {
                return mode;
            }
        }
        return std::nullopt;
    }

    Optional<PieceColor> ParseColor(eastl::string_view id) {
        if (id == kWhiteId) {
            return PieceColor::White;
        }
        if (id == kBlackId) {
            return PieceColor::Black;
        }
        return std::nullopt;
    }

    std::expected<void, ChoiceError> OpponentChoice::SetMode(eastl::string_view id) {
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

    std::expected<void, ChoiceError> OpponentChoice::SetColor(eastl::string_view id) {
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

    std::expected<void, ChoiceError> OpponentChoice::SetEngine(eastl::string_view id) {
        const Optional<OpponentEngine> engine = FindOpponentEngine(id);
        if (!engine) {
            return std::unexpected(BadId("engine", id));
        }
        return FromController(_opponent.SetEngine(*engine));
    }

    std::expected<void, ChoiceError> OpponentChoice::SetLevel(eastl::string_view id) {
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

    eastl::string_view OpponentChoice::ModeId() const noexcept {
        return OpponentModeId(Mode());
    }

    eastl::string_view OpponentChoice::ColorId() const noexcept {
        return PieceColorId(Color());
    }

    eastl::string_view OpponentChoice::EngineId() const noexcept {
        return OpponentEngineId(_opponent.GetEngine());
    }

    eastl::string_view OpponentChoice::LevelId() const noexcept {
        return GetOpponentLevels()[_opponent.GetLevel()].id;
    }

}  // namespace BECore::Chess
