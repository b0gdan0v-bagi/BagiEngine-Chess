#pragma once

#include <Chess/Game/Position.h>

namespace BECore::Chess {

    /// Why the draft did not take an edit.
    CORE_ENUM(EditRefusal, uint8_t, OffBoard, TooManyPieces)

    /// The four castling rights of the draft, in the order of a FEN's "KQkq".
    CORE_ENUM(CastlingRight, uint8_t, WhiteKingSide, WhiteQueenSide, BlackKingSide, BlackQueenSide)

    /// What a click on a square does: puts this piece, or (type None) takes the piece off.
    struct EditorTool {
        PieceType type = PieceType::Pawn;
        PieceColor color = PieceColor::White;

        [[nodiscard]] constexpr bool IsEraser() const noexcept {
            return type == PieceType::None;
        }
        constexpr bool operator==(const EditorTool& other) const noexcept {
            return type == other.type && (IsEraser() || color == other.color);
        }
    };

    /// The pieces of the tool bar in its order (king first); the eraser is PieceType::None and not in this list.
    [[nodiscard]] Span<const PieceType> EditorToolPieces();

    /// The name of a tool in the scene and the control API ("king" ... "pawn", lower case; "erase" for the eraser); empty for a type that is no tool.
    [[nodiscard]] StringView EditorToolId(PieceType type);

    /// The tool with this id (exactly as written: ids are lower case): the piece type, PieceType::None for the eraser, or nothing.
    [[nodiscard]] Optional<PieceType> FindEditorTool(StringView id);

    /**
     * @brief A position under construction: 64 squares, the side to move, four castling flags and the picked tool.
     *
     * The draft owns no rule of chess: Build() is Position::FromFen(ToFen()), so a draft is playable exactly when FromFen accepts
     * it. The only rules it keeps are its own: at most kMaxPiecesPerSide pieces of a color (a ChessGameModel has that many handles),
     * and a castling flag stays on only while its king and rook stand on their squares (FromFen would refuse the position
     * otherwise), so a flag goes off when one of them leaves and never comes back by itself. En passant is never set and the
     * clocks are "0 1": a position built here starts a new game.
     */
    class PositionEditor {
    public:
        using Squares = Array<Piece, kSquareCount>;

        /// An empty board, White to move, no rights: Build() refuses it until both kings stand.
        void Clear();
        void SetStart();
        /// The pieces, the side to move and the castling rights of @p position; its en passant square and clocks are dropped.
        void Load(const Position& position);
        /// Loads the position of a FEN text; the draft stays as it was on an error.
        [[nodiscard]] std::expected<void, FenError> LoadFen(StringView fen);

        /// Puts @p piece on @p square (an empty piece takes the square's piece off); putting what is already there changes nothing.
        [[nodiscard]] std::expected<void, EditRefusal> Put(Square square, Piece piece);
        /// Takes the piece off; whether there was one.
        bool Remove(Square square);
        /// The click of the picked tool: the eraser or the same piece already there takes the piece off, else the piece is put. Whether the draft changed; a refused put leaves GetLastRefusal().
        bool Apply(Square square);
        /// What the latest Put, Remove or Apply refused, nothing when it was taken.
        [[nodiscard]] Optional<EditRefusal> GetLastRefusal() const noexcept {
            return _lastRefusal;
        }

        void SetSideToMove(PieceColor color);
        [[nodiscard]] PieceColor GetSideToMove() const noexcept {
            return _sideToMove;
        }

        /// Sets a flag; turning one on while !CanCastle(right) is ignored.
        void SetCastling(CastlingRight right, bool enabled);
        [[nodiscard]] bool GetCastling(CastlingRight right) const noexcept;
        /// Whether the king and the rook of that right stand on their home squares.
        [[nodiscard]] bool CanCastle(CastlingRight right) const noexcept;

        void SetTool(EditorTool tool);
        [[nodiscard]] EditorTool GetTool() const noexcept {
            return _tool;
        }

        [[nodiscard]] const Squares& GetSquares() const noexcept {
            return _squares;
        }
        [[nodiscard]] Piece GetPieceAt(Square square) const noexcept {
            return square.IsInside() ? _squares[static_cast<size_t>(square.Index())] : Piece{};
        }
        [[nodiscard]] int CountPieces(PieceColor color) const noexcept;

        /// The FEN of the draft, with "-" for en passant and "0 1" for the clocks.
        [[nodiscard]] String ToFen() const;
        [[nodiscard]] std::expected<Position, FenError> Build() const;

        /// Grows with every change of the draft or the tool, with Clear, SetStart and Load even when they change nothing, and with MarkClosed(); never goes back, also when the draft ends,
        /// so a sum that holds it never goes back either.
        [[nodiscard]] uint64_t GetRevision() const noexcept {
            return _revision;
        }

        /// For an owner that ends the draft without changing the game (a control client sees the draft go): counts as one change
        void MarkClosed() noexcept {
            ++_revision;
        }

    private:
        void DropUnavailableRights() noexcept;

        Squares _squares{};
        PieceColor _sideToMove = PieceColor::White;
        int _castlingFlags = 0;  // the bits of Position::CastlingRights()
        EditorTool _tool;
        Optional<EditRefusal> _lastRefusal;
        uint64_t _revision = 0;
    };

}  // namespace BECore::Chess
