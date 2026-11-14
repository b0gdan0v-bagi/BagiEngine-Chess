#include "OpponentText.h"

namespace BECore::Chess {

    namespace OpponentText {

        String ModeLabel(bool computer) {
            return computer ? "Computer" : "Two players";
        }

        String ColorLabel(PieceColor color) {
            return color == PieceColor::White ? "White" : "Black";
        }

        String Error(const EngineError& error) {
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
            return String(kUnknownErrorSentence.data(), kUnknownErrorSentence.size());
        }

    }  // namespace OpponentText

}  // namespace BECore::Chess
