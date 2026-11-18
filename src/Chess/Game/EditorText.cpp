#include "EditorText.h"

namespace BECore::Chess::EditorText {

    StringView Refusal(FenError error) {
        switch (error) {
            case FenError::WrongFieldCount:
            case FenError::BadPiecePlacement:
            case FenError::BadSideToMove:
            case FenError::BadCastling:
            case FenError::BadEnPassant:
            case FenError::BadClock:
                return "Not a position";
            case FenError::BadKingCount:
                return "Needs one king each";
            case FenError::PawnOnBackRank:
                return "Pawn on a back rank";
            case FenError::TooManyPieces:
                return "Too many pieces";
            case FenError::OpponentInCheck:
                return "Other king in check";
        }
        return "Not a position";
    }

    StringView Refusal(EditRefusal refusal) {
        switch (refusal) {
            case EditRefusal::OffBoard:
                return "Not on the board";
            case EditRefusal::TooManyPieces:
                return "Too many pieces";
        }
        return "Not on the board";
    }

    StringView Clipboard(ClipboardStatus status) {
        switch (status) {
            case ClipboardStatus::Ok:
                return "FEN copied";
            case ClipboardStatus::Empty:
                return "Clipboard is empty";
            case ClipboardStatus::Refused:
                return "Clipboard refused";
            case ClipboardStatus::Unavailable:
                return "No clipboard access";
        }
        return "No clipboard access";
    }

}  // namespace BECore::Chess::EditorText
