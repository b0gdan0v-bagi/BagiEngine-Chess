#include "OpponentText.h"

namespace BECore::Chess {

    namespace OpponentText {

        eastl::string SideLabel(std::optional<PieceColor> computerPlays) {
            if (!computerPlays) {
                return "Two players";
            }
            // The program plays one color, the person the other
            return *computerPlays == PieceColor::Black ? "You: White" : "You: Black";
        }

        eastl::string ModeLabel(bool computer) {
            return computer ? "Computer" : "Two players";
        }

        eastl::string ColorLabel(PieceColor color) {
            return color == PieceColor::White ? "White" : "Black";
        }

        eastl::string LevelLabel(const OpponentLevel& level) {
            return Format("Level: {}", level.name);
        }

        eastl::string EngineLabel(OpponentEngine engine) {
            const eastl::string_view name = OpponentEngineName(engine);
            return eastl::string(name.data(), name.size());
        }

        eastl::string Error(const EngineError& error) {
            switch (error.kind) {
                case EngineErrorKind::LaunchFailed:
                    return "Engine not found";
                case EngineErrorKind::Timeout:
                    return "Engine did not answer";
                case EngineErrorKind::ProcessDied:
                    return "Engine stopped";
                case EngineErrorKind::ProtocolError:
                    return "Not a UCI engine";
                case EngineErrorKind::IllegalMove:
                    return "Illegal engine move";
                case EngineErrorKind::NoMove:
                    return "Engine has no move";
                case EngineErrorKind::BadRequest:
                    return "Engine request failed";
                case EngineErrorKind::Cancelled:
                    return "Engine was cancelled";
                case EngineErrorKind::Unsupported:
                    return "Engine not supported here";
            }
            return eastl::string(kUnknownErrorSentence.data(), kUnknownErrorSentence.size());
        }

    }  // namespace OpponentText

}  // namespace BECore::Chess
