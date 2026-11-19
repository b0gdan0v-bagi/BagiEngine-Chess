#include <Chess/Components/PieceSprite.h>
#include <Chess/Game/ChessTypes.h>
#include <tests/ChessPieceArt.h>
#include <tests/ChessTestUtil.h>
#include <tests/ShippedScene.h>

/// The shipped piece art held to `config/TextureLibrary.xml`: the atlas `assets/atlases/pieces.png` and the sources it was packed from, files of the source tree whose PNG headers are
/// read and never decoded (that the pieces look right is for the eye, `artgen/presets/chess_piece.json`; that the atlas is what the packer makes of the sources is `ChessPieceAtlasTest`)
namespace BECore::Chess::Tests {

    namespace {

        constexpr const char* kSourceFolder = "assets/artgen/";
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

        bool LoadLibrary(pugi::xml_document& document) {
            const std::filesystem::path file = ShippedConfigFolder() / "TextureLibrary.xml";
            if (!document.load_file(file.c_str())) {
                ADD_FAILURE() << "the shipped texture library does not parse";
                return false;
            }
            return true;
        }

        /// The `<sprite>` named @p name, empty when the library has none
        pugi::xml_node SpriteOf(const pugi::xml_document& document, const String& name) {
            return document.select_node("/root/sprites").node().find_child_by_attribute("sprite", "name", name.c_str());
        }

        /// The file a sprite was packed from (`<atlas><source texturePath>`), empty when it is not packed
        String SourcePathOf(const pugi::xml_node& sprite) {
            return String(sprite.child("atlas").child("source").attribute("texturePath").as_string());
        }

        struct SpriteRect {
            float x = 0;
            float y = 0;
            float w = 0;
            float h = 0;
        };

        SpriteRect SrcRectOf(const pugi::xml_node& sprite) {
            const pugi::xml_node rect = sprite.child("srcRect");
            return SpriteRect{rect.attribute("x").as_float(), rect.attribute("y").as_float(), rect.attribute("w").as_float(), rect.attribute("h").as_float()};
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

    TEST(ChessPieceArtFilesTest, EveryPieceIsOneSpriteOfTheAtlasGroup) {
        pugi::xml_document library;
        ASSERT_TRUE(LoadLibrary(library));
        const pugi::xml_node sprites = library.select_node("/root/sprites").node();
        for (const Piece piece : AllPieces()) {
            const String name = String(PieceSpriteId(piece).ToStringView());
            SCOPED_TRACE(name.c_str());
            size_t count = 0;
            for (const pugi::xml_node sprite : sprites.children("sprite")) {
                count += String(sprite.attribute("name").as_string()) == name ? 1 : 0;
            }
            EXPECT_EQ(count, 1u) << "the library names the sprite once";
            const pugi::xml_node sprite = SpriteOf(library, name);
            EXPECT_EQ(String(sprite.attribute("texturePath").as_string()), String(kPiecesAtlasPath));
            EXPECT_EQ(String(sprite.child("atlas").attribute("name").as_string()), String(kPiecesGroup));
        }
    }

    TEST(ChessPieceArtFilesTest, TheAtlasIsAnRgbaPngWithinItsLimit) {
        const PngHeader header = ReadPngHeader(FileOfTexturePath(kPiecesAtlasPath));
        ASSERT_TRUE(header.valid) << "not a PNG: " << kPiecesAtlasPath;
        EXPECT_EQ(header.bitDepth, kBitDepth);
        EXPECT_EQ(header.colorType, kColorTypeRgba) << "the cut-out needs an alpha channel";
        EXPECT_GT(header.width, 0u);
        EXPECT_GT(header.height, 0u);
        EXPECT_LE(std::max(header.width, header.height), kMaxAtlasSide);
    }

    TEST(ChessPieceArtFilesTest, EveryPieceRectLiesInTheAtlasWithoutOverlapAndAllShareOneSize) {
        pugi::xml_document library;
        ASSERT_TRUE(LoadLibrary(library));
        const PngHeader atlas = ReadPngHeader(FileOfTexturePath(kPiecesAtlasPath));
        ASSERT_TRUE(atlas.valid);
        Vector<SpriteRect> rects;
        for (const Piece piece : AllPieces()) {
            const String name = String(PieceSpriteId(piece).ToStringView());
            SCOPED_TRACE(name.c_str());
            const SpriteRect rect = SrcRectOf(SpriteOf(library, name));
            EXPECT_GE(rect.x, 0.0F);
            EXPECT_GE(rect.y, 0.0F);
            EXPECT_GT(rect.w, 0.0F);
            EXPECT_GT(rect.h, 0.0F);
            EXPECT_LE(rect.x + rect.w, static_cast<float>(atlas.width));
            EXPECT_LE(rect.y + rect.h, static_cast<float>(atlas.height));
            if (!rects.empty()) {
                EXPECT_EQ(rect.w, rects.front().w) << "one set, one frame";
                EXPECT_EQ(rect.h, rects.front().h) << "one set, one frame";
            }
            rects.push_back(rect);
        }
        for (size_t i = 0; i < rects.size(); ++i) {
            for (size_t j = i + 1; j < rects.size(); ++j) {
                EXPECT_FALSE(Overlap(rects[i], rects[j])) << "pieces " << i << " and " << j << " share pixels of the atlas";
            }
        }
    }

    TEST(ChessPieceArtFilesTest, EveryPieceSourceIsAnRgbaPngOfOneFrameWithItsProvenanceBeside) {
        pugi::xml_document library;
        ASSERT_TRUE(LoadLibrary(library));
        PngHeader first;
        for (const Piece piece : AllPieces()) {
            const String name = String(PieceSpriteId(piece).ToStringView());
            SCOPED_TRACE(name.c_str());
            const String source = SourcePathOf(SpriteOf(library, name));
            EXPECT_EQ(source, Format("{}{}.png", kSourceFolder, name)) << "the sprite is not packed from the file of its name";
            const std::filesystem::path file = FileOfTexturePath(source);
            ASSERT_TRUE(std::filesystem::exists(file)) << source.c_str();
            const PngHeader header = ReadPngHeader(file);
            ASSERT_TRUE(header.valid) << "not a PNG: " << source.c_str();
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

            std::filesystem::path provenance = file;
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
