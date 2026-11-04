#include "OpponentText.h"

namespace BECore::Chess {

    namespace OpponentText {

        namespace {

            // The sides in the order of the button (OpponentController::CycleSide): nobody, then the program plays Black ("You: White"), then White ("You: Black")
            constexpr eastl::array<std::optional<PieceColor>, 3> kComputerSides{std::nullopt, PieceColor::Black, PieceColor::White};

            /// Replaces @p longest by @p candidate when the candidate has more characters (the first of equals stays)
            void KeepLonger(eastl::string& longest, eastl::string candidate) {
                if (candidate.size() > longest.size()) {
                    longest = std::move(candidate);
                }
            }

        }  // namespace

        eastl::string SideLabel(std::optional<PieceColor> computerPlays) {
            if (!computerPlays) {
                return "Two players";
            }
            // The program plays one color, the person the other
            return *computerPlays == PieceColor::Black ? "You: White" : "You: Black";
        }

        eastl::string LevelLabel(const OpponentLevel& level) {
            return Format("Level: {}", level.name);
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
                    return "Engine made an illegal move";
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

        eastl::string LongestSideLabel() {
            eastl::string longest;
            for (const std::optional<PieceColor> side : kComputerSides) {
                KeepLonger(longest, SideLabel(side));
            }
            return longest;
        }

        eastl::string LongestLevelLabel() {
            eastl::string longest;
            for (const OpponentLevel& level : GetOpponentLevels()) {
                KeepLonger(longest, LevelLabel(level));
            }
            return longest;
        }

        eastl::string LongestError() {
            eastl::string longest;
            for (const EngineErrorKind kind : EnumUtils<EngineErrorKind>::Values()) {
                KeepLonger(longest, Error(EngineError{kind, {}}));
            }
            return longest;
        }

    }  // namespace OpponentText

}  // namespace BECore::Chess
