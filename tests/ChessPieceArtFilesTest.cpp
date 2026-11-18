#include <Chess/Components/PieceSprite.h>
#include <Chess/Game/ChessTypes.h>
#include <tests/ChessTestUtil.h>
#include <tests/ShippedScene.h>

/// The shipped piece art held to `config/TextureLibrary.xml`: files of the source tree, never decoded (that the pieces look right is for the eye, `artgen/presets/chess_piece.json`)
namespace BECore::Chess::Tests {

    namespace {

        constexpr const char* kArtFolder = "assets/artgen/";
        constexpr uint32_t kMaxSide = 256;  // `accept.maxSize` of the preset

        constexpr size_t kSignatureSize = 8;
        constexpr size_t kIhdrTypeOffset = 12;
        constexpr size_t kWidthOffset = 16;
        constexpr size_t kHeightOffset = 20;
        constexpr size_t kBitDepthOffset = 24;
        constexpr size_t kColorTypeOffset = 25;
        constexpr size_t kHeaderSize = 26;
        constexpr uint8_t kBitDepth = 8;
        constexpr uint8_t kColorTypeRgba = 6;
        constexpr uint8_t kPngSignature[kSignatureSize] = {0x89, 'P', 'N', 'G', '\r', '\n', 0x1A, '\n'};

        std::filesystem::path GameRoot() {
            return ShippedConfigFolder().parent_path();
        }

        Vector<Piece> AllPieces() {
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

        bool LoadLibrary(pugi::xml_document& document) {
            const std::filesystem::path file = ShippedConfigFolder() / "TextureLibrary.xml";
            if (!document.load_file(file.c_str())) {
                ADD_FAILURE() << "the shipped texture library does not parse";
                return false;
            }
            return true;
        }

        /// The `texturePath` of the sprite named @p name, empty when the library has none
        String TexturePathOf(const pugi::xml_document& document, const String& name) {
            const pugi::xml_node sprite = document.select_node("/root/sprites").node().find_child_by_attribute("sprite", "name", name.c_str());
            return String(sprite.attribute("texturePath").as_string());
        }

        struct PngHeader {
            bool valid = false;
            uint32_t width = 0;
            uint32_t height = 0;
            uint8_t bitDepth = 0;
            uint8_t colorType = 0;
        };

        uint32_t BigEndian32(const Vector<uint8_t>& bytes, size_t offset) {
            return (static_cast<uint32_t>(bytes[offset]) << 24) | (static_cast<uint32_t>(bytes[offset + 1]) << 16) | (static_cast<uint32_t>(bytes[offset + 2]) << 8) |
                   static_cast<uint32_t>(bytes[offset + 3]);
        }

        PngHeader ReadPngHeader(const std::filesystem::path& path) {
            Vector<uint8_t> bytes(kHeaderSize, 0);
            //-NoStd: file layer (engine-file-layer)
            std::ifstream file(path, std::ios::binary);
            file.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
            PngHeader header;
            if (static_cast<size_t>(file.gcount()) != kHeaderSize || !std::equal(std::begin(kPngSignature), std::end(kPngSignature), bytes.begin()) ||
                std::memcmp(bytes.data() + kIhdrTypeOffset, "IHDR", 4) != 0) {
                return header;
            }
            header.valid = true;
            header.width = BigEndian32(bytes, kWidthOffset);
            header.height = BigEndian32(bytes, kHeightOffset);
            header.bitDepth = bytes[kBitDepthOffset];
            header.colorType = bytes[kColorTypeOffset];
            return header;
        }

        std::filesystem::path FileOfTexturePath(const String& texturePath) {
            return GameRoot() / std::filesystem::path(ToStd(texturePath));
        }

    }  // namespace

    TEST(ChessPieceArtFilesTest, EveryPieceHasASpriteOfItsOwnInTheArtFolder) {
        pugi::xml_document library;
        ASSERT_TRUE(LoadLibrary(library));
        Vector<String> paths;
        for (const Piece piece : AllPieces()) {
            const String name = String(PieceSpriteId(piece).ToStringView());
            SCOPED_TRACE(name.c_str());
            const String texturePath = TexturePathOf(library, name);
            EXPECT_TRUE(StringView(texturePath).starts_with(kArtFolder)) << "the library has no sprite of this name under " << kArtFolder << ": '" << texturePath.c_str() << "'";
            EXPECT_EQ(std::count(paths.begin(), paths.end(), texturePath), 0) << "two pieces share a file";
            paths.push_back(texturePath);
        }
    }

    TEST(ChessPieceArtFilesTest, EveryPieceFileIsAnRgbaPngAndTheTwelveShareOneFrame) {
        pugi::xml_document library;
        ASSERT_TRUE(LoadLibrary(library));
        PngHeader first;
        for (const Piece piece : AllPieces()) {
            const String name = String(PieceSpriteId(piece).ToStringView());
            SCOPED_TRACE(name.c_str());
            const String texturePath = TexturePathOf(library, name);
            ASSERT_FALSE(texturePath.empty()) << "no sprite in the library";
            const std::filesystem::path file = FileOfTexturePath(texturePath);
            ASSERT_TRUE(std::filesystem::exists(file)) << texturePath.c_str();
            const PngHeader header = ReadPngHeader(file);
            ASSERT_TRUE(header.valid) << "not a PNG: " << texturePath.c_str();
            EXPECT_EQ(header.bitDepth, kBitDepth);
            EXPECT_EQ(header.colorType, kColorTypeRgba) << "the cut-out needs an alpha channel";
            EXPECT_GT(header.width, 0u);
            EXPECT_GT(header.height, 0u);
            EXPECT_LE(std::max(header.width, header.height), kMaxSide);
            if (!first.valid) {
                first = header;
            }
            EXPECT_EQ(header.width, first.width) << "one set, one frame";
            EXPECT_EQ(header.height, first.height) << "one set, one frame";
        }
    }

    TEST(ChessPieceArtFilesTest, EveryPieceFileHasItsProvenanceBeside) {
        pugi::xml_document library;
        ASSERT_TRUE(LoadLibrary(library));
        for (const Piece piece : AllPieces()) {
            const String name = String(PieceSpriteId(piece).ToStringView());
            SCOPED_TRACE(name.c_str());
            std::filesystem::path provenance = FileOfTexturePath(TexturePathOf(library, name));
            provenance.replace_extension(".artgen.json");
            EXPECT_TRUE(std::filesystem::exists(provenance)) << provenance.filename().string();
        }
    }

    TEST(ChessPieceArtFilesTest, NoSpriteOfTheLibraryNamesAFileThatIsMissing) {
        pugi::xml_document library;
        ASSERT_TRUE(LoadLibrary(library));
        for (const pugi::xml_node sprite : library.select_node("/root/sprites").node().children("sprite")) {
            const String texturePath(sprite.attribute("texturePath").as_string());
            EXPECT_TRUE(std::filesystem::exists(FileOfTexturePath(texturePath))) << sprite.attribute("name").as_string() << ": " << texturePath.c_str();
        }
    }

}  // namespace BECore::Chess::Tests
