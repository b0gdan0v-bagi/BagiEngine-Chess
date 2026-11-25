#include <BECore/Config/XmlDocument.h>
#include <BECore/Reflection/XmlDeserializer.h>
#include <BECore/Resource/TextureLibrary.h>
#include <Chess/Components/PieceSprite.h>
#include <Chess/Game/ChessTypes.h>
#include <Common/Printers.h>
#include <Common/TempPath.h>
#include <Widgets/TextureEditor/SpriteAtlas.h>
#include <tests/ChessPieceArt.h>
#include <tests/FakeEngineUtil.h>
#include <tests/ShippedScene.h>

#if BE_WITH_EDITOR

/**
 * The twelve pieces packed into the atlas group `pieces` by the engine's packer, the code behind the Texture editor's Pack button. The normal tests pack into a temp folder and never touch
 * the checkout; the disabled one is how `assets/atlases/pieces.png` and the sprites of `config/TextureLibrary.xml` are made (`config/CLAUDE.md`)
 */
namespace BECore::Chess::Tests {

    namespace {

        constexpr size_t kPieceCount = 12;
        constexpr float kPieceWidth = 244.0F;
        constexpr float kPieceHeight = 256.0F;

        Path ShippedLibraryFile() {
            return ShippedConfigFolder() / "TextureLibrary.xml";
        }

        bool LoadShippedLibrary(TextureLibrary& library) {
            XmlDeserializer deserializer;
            if (!deserializer.LoadFromFile(ShippedLibraryFile())) {
                ADD_FAILURE() << "the shipped texture library does not load";
                return false;
            }
            library.Load(deserializer);
            return true;
        }

        /// Puts the twelve pieces into the group (those already in it stay) and writes the atlas under @p assetsDir; the sources are read from the game's tree. The library holds the
        /// rewritten entries on success
        std::expected<AtlasWritten, String> PackPiecesAtlas(TextureLibrary& library, const Path& assetsDir) {
            for (const Piece piece : AllPieces()) {
                const PoolString name = PieceSpriteId(piece, GraphicsPreset::Beer);
                const SpriteEntry* entry = library.GetSprite(name);
                if (entry == nullptr) {
                    return std::unexpected(Format("the library has no sprite {}", name.ToStringView()));
                }
                if (!entry->atlas.has_value() || entry->atlas->name.ToStringView() != kPiecesGroup) {
                    const auto set = SetSpriteAtlas(library, name, kPiecesGroup);
                    if (!set.has_value()) {
                        return std::unexpected(set.error());
                    }
                }
            }
            PackSettings settings;
            settings.maxSide = kMaxAtlasSide;
            const auto resolve = [](StringView virtualPath) { return GameRoot() / virtualPath; };
            return WriteAtlas(library, kPiecesGroup, settings, AtlasIo{assetsDir, resolve});
        }

        bool SameRect(const Rect& a, const Rect& b) {
            return a.x == b.x && a.y == b.y && a.w == b.w && a.h == b.h;
        }

        bool SamePack(const Optional<AtlasPackRecord>& a, const Optional<AtlasPackRecord>& b) {
            if (a.has_value() != b.has_value()) {
                return false;
            }
            return !a.has_value() || (a->padding == b->padding && a->sizeMode == b->sizeMode && a->maxSide == b->maxSide);
        }

        bool SameEntry(const SpriteEntry& a, const SpriteEntry& b) {
            if (a.name != b.name || a.texturePath != b.texturePath || !SameRect(a.srcRect, b.srcRect) || a.atlas.has_value() != b.atlas.has_value()) {
                return false;
            }
            if (!a.atlas.has_value()) {
                return true;
            }
            if (a.atlas->name != b.atlas->name || a.atlas->source.has_value() != b.atlas->source.has_value() || !SamePack(a.atlas->pack, b.atlas->pack)) {
                return false;
            }
            return !a.atlas->source.has_value() || (a.atlas->source->texturePath == b.atlas->source->texturePath && SameRect(a.atlas->source->rect, b.atlas->source->rect));
        }

    }  // namespace

    TEST(ChessPieceAtlasTest, PackingTheShippedSourcesMakesOneAtlasOfTheTwelve) {
        TextureLibrary library;
        ASSERT_TRUE(LoadShippedLibrary(library));
        Vector<PoolString> namesBefore;
        for (const SpriteEntry& entry : library.GetAll()) {
            namesBefore.push_back(entry.name);
        }
        ASSERT_EQ(namesBefore.size(), 2 * kPieceCount) << "the library holds the Beer set and the Classic set";
        Vector<SpriteEntry> classicBefore;
        for (const PoolString name : SpriteNamesOf(GraphicsPreset::Classic)) {
            const SpriteEntry* entry = library.GetSprite(name);
            ASSERT_NE(entry, nullptr);
            classicBefore.push_back(*entry);
        }

        const TempDirectory temp;
        const auto written = PackPiecesAtlas(library, temp.Path() / "assets");
        ASSERT_TRUE(written.has_value()) << written.error().c_str();
        EXPECT_EQ(written->changed.size(), kPieceCount);
        EXPECT_LE(written->width, kMaxAtlasSide);
        EXPECT_LE(written->height, kMaxAtlasSide);
        EXPECT_EQ(written->texturePath.ToStringView(), AtlasTexturePath(kPiecesGroup).ToStringView());

        const auto atlas = LoadRgbaImage(temp.Path() / "assets" / kAtlasAssetsFolder.data() / "pieces.png");
        ASSERT_TRUE(atlas.has_value()) << atlas.error().c_str();
        EXPECT_EQ(atlas->width, written->width);
        EXPECT_EQ(atlas->height, written->height);

        ASSERT_EQ(library.GetAll().size(), namesBefore.size());
        for (size_t i = 0; i < namesBefore.size(); ++i) {
            EXPECT_EQ(library.GetAll()[i].name, namesBefore[i]) << "packing reorders the library";
        }
        for (const SpriteEntry& before : classicBefore) {
            const SpriteEntry* after = library.GetSprite(before.name);
            ASSERT_NE(after, nullptr);
            EXPECT_TRUE(SameEntry(before, *after)) << "the packer touched a classic sprite: " << String(before.name.ToStringView()).c_str();
        }

        Vector<Rect> placed;
        for (const Piece piece : AllPieces()) {
            const PoolString name = PieceSpriteId(piece, GraphicsPreset::Beer);
            SCOPED_TRACE(String(name.ToStringView()).c_str());
            const SpriteEntry* entry = library.GetSprite(name);
            ASSERT_NE(entry, nullptr);
            ASSERT_TRUE(entry->atlas.has_value());
            EXPECT_EQ(entry->atlas->name.ToStringView(), kPiecesGroup);
            EXPECT_EQ(entry->texturePath.ToStringView(), AtlasTexturePath(kPiecesGroup).ToStringView());
            ASSERT_TRUE(entry->atlas->source.has_value());
            EXPECT_EQ(entry->atlas->source->texturePath.ToStringView(), Format("assets/artgen/{}.png", name.ToStringView()));
            EXPECT_TRUE(SameRect(entry->atlas->source->rect, Rect(0, 0, kPieceWidth, kPieceHeight)));

            const Rect& rect = entry->srcRect;
            EXPECT_EQ(rect.w, kPieceWidth);
            EXPECT_EQ(rect.h, kPieceHeight);
            EXPECT_GE(rect.x, 0.0F);
            EXPECT_GE(rect.y, 0.0F);
            EXPECT_LE(rect.x + rect.w, static_cast<float>(written->width));
            EXPECT_LE(rect.y + rect.h, static_cast<float>(written->height));
            placed.push_back(rect);
        }
        for (size_t i = 0; i < placed.size(); ++i) {
            for (size_t j = i + 1; j < placed.size(); ++j) {
                EXPECT_FALSE(Overlap(placed[i], placed[j])) << "pieces " << i << " and " << j << " overlap in the atlas";
            }
        }
    }

    /// The pixels, not the PNG bytes: the compressed bytes may differ between platforms
    TEST(ChessPieceAtlasTest, PackingTheSourcesAgainGivesTheShippedAtlasAndEntries) {
        TextureLibrary shipped;
        ASSERT_TRUE(LoadShippedLibrary(shipped));
        TextureLibrary repacked;
        ASSERT_TRUE(LoadShippedLibrary(repacked));

        const TempDirectory temp;
        const auto written = PackPiecesAtlas(repacked, temp.Path() / "assets");
        ASSERT_TRUE(written.has_value()) << written.error().c_str();

        ASSERT_EQ(repacked.GetAll().size(), shipped.GetAll().size());
        for (size_t i = 0; i < shipped.GetAll().size(); ++i) {
            EXPECT_TRUE(SameEntry(shipped.GetAll()[i], repacked.GetAll()[i]))
                << "the shipped entry of " << String(shipped.GetAll()[i].name.ToStringView()).c_str() << " is not what the packer makes; run the disabled test";
        }

        const auto fresh = LoadRgbaImage(temp.Path() / "assets" / kAtlasAssetsFolder.data() / "pieces.png");
        ASSERT_TRUE(fresh.has_value()) << fresh.error().c_str();
        const auto onDisk = LoadRgbaImage(GameRoot() / "assets" / kAtlasAssetsFolder.data() / "pieces.png");
        ASSERT_TRUE(onDisk.has_value()) << onDisk.error().c_str();
        EXPECT_EQ(fresh->width, onDisk->width);
        EXPECT_EQ(fresh->height, onDisk->height);
        EXPECT_TRUE(fresh->pixels == onDisk->pixels) << "assets/atlases/pieces.png is not what the packer makes of the sources; run ChessPieceAtlasTest.DISABLED_RewriteTheShippedAtlas";
    }

    /// Run by hand (`--gtest_also_run_disabled_tests --gtest_filter=ChessPieceAtlasTest.DISABLED_*`) when a source piece changed outside Accept of the Generate panel: it writes the checkout
    TEST(ChessPieceAtlasTest, DISABLED_RewriteTheShippedAtlas) {
        TextureLibrary library;
        ASSERT_TRUE(LoadShippedLibrary(library));
        const auto written = PackPiecesAtlas(library, GameRoot() / "assets");
        ASSERT_TRUE(written.has_value()) << written.error().c_str();

        XmlDocument document;
        ASSERT_TRUE(document.LoadFromFile(ShippedLibraryFile()));
        ASSERT_TRUE(library.WriteInto(document.GetRoot().GetPugiNode()));
        ASSERT_TRUE(document.SaveToFile(ShippedLibraryFile()));
    }

}  // namespace BECore::Chess::Tests

#endif  // BE_WITH_EDITOR
