#pragma once

#include <Chess/Game/ChessTypes.h>
#include <filesystem>
#include <tests/ShippedScene.h>

/// What `ChessPieceArtFilesTest` (headers of the shipped art) and `ChessPieceAtlasTest` (the packer's output) both hold the art to
namespace BECore::Chess::Tests {

    inline constexpr const char* kPiecesAtlasPath = "assets/atlases/pieces.png";
    inline constexpr const char* kPiecesGroup = "pieces";
    // Four of the twelve pieces to a row make 1024 x 1024; the packer's default cap of 4096 would pick a 256 x 4096 strip of the same area (the smaller width wins a tie)
    inline constexpr uint32_t kMaxAtlasSide = 1024;

    inline std::filesystem::path GameRoot() {
        return ShippedConfigFolder().parent_path();
    }

    inline Vector<Piece> AllPieces() {
        Vector<Piece> pieces;
        for (const PieceColor color : EnumUtils<PieceColor>::Values()) {
            for (const PieceType type : EnumUtils<PieceType>::Values()) {
                if (type != PieceType::None) {
                    pieces.push_back(Piece{type, color});
                }
            }
        }
        return pieces;
    }

    /// True when the two rectangles (anything with x, y, w, h) share pixels
    template <typename RectA, typename RectB>
    bool Overlap(const RectA& a, const RectB& b) {
        return a.x < b.x + b.w && b.x < a.x + a.w && a.y < b.y + b.h && b.y < a.y + a.h;
    }

}  // namespace BECore::Chess::Tests
