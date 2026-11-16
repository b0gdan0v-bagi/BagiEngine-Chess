#include <BECore/Reflection/XmlDeserializer.h>
#include <Chess/Game/ChessTypes.h>
#include <Chess/Game/Position.h>
#include <tests/ShippedScene.h>

/**
 * The shipped scene is `config/SceneConfig.xml` with the nodes in the files of `config/nodes/`: this holds the split, so that a later entry cannot break it unnoticed. The order of the
 * nodes is the draw and input order (`src/Chess/Components/CLAUDE.md`, "Draw order"), so a reorder fails here on purpose. Plain files, no scene services.
 */
namespace BECore::Chess::Tests {

    namespace {

        constexpr int kBoardSize = 8;
        constexpr int kPieceCount = 32;
        constexpr float kSquareSize = 80.0f;  ///< a piece node is the whole square (ChessPiece.xml)

        struct UiNode {
            const char* name;
            int layer;
        };

        /// The nodes before the pieces, in the order of the scene
        constexpr UiNode kUiNodes[] = {
            {"Board", -1},     {"BoardCoordinates", -1}, {"Material", 5},      {"MovePanel", 4},       {"Hud", 10},
            {"MenuPanel", 20}, {"OpponentPanel", 20},    {"MovesSection", 20}, {"PromotionPanel", 20}, {"Result", 1},
        };
        constexpr size_t kUiCount = std::size(kUiNodes);

        pugi::xml_node SceneNodes(const pugi::xml_document& document) {
            return document.select_node("/SceneManager/scenes/scene[@name='ChessScene']/nodes").node();
        }

        Vector<pugi::xml_node> NodesOf(const pugi::xml_node& nodes) {
            Vector<pugi::xml_node> found;
            for (const pugi::xml_node node : nodes.children("node")) {
                found.push_back(node);
            }
            return found;
        }

        pugi::xml_node ComponentOf(const pugi::xml_node& node, const char* type) {
            return node.child("components").find_child_by_attribute("component", "type", type);
        }

        /// The sprite of the piece the starting position has on the square: `WhiteRook`
        String ExpectedSpriteId(Square square) {
            const Piece piece = Position::Start().GetPieceAt(square);
            return Format("{}{}", EnumUtils<PieceColor>::ToString(piece.color), EnumUtils<PieceType>::ToString(piece.type));
        }

        /// The name of its node: `WhiteRook_a1`
        String ExpectedPieceName(Square square) {
            return Format("{}_{}{}", ExpectedSpriteId(square), static_cast<char>('a' + square.file), square.rank + 1);
        }

    }  // namespace

    TEST(ChessSceneFilesTest, EveryReferenceAndOverrideOfTheSceneLoads) {
        pugi::xml_document document;
        ASSERT_TRUE(LoadShippedSceneDocument(document)) << ShippedSceneConfig().string();
    }

    TEST(ChessSceneFilesTest, TheExpandedSceneDeserializesWithoutErrors) {
        pugi::xml_document document;
        ASSERT_TRUE(LoadShippedSceneDocument(document));
        XmlDeserializer deserializer;
        ASSERT_TRUE(deserializer.LoadFromXmlNode(XmlNode{document.document_element()}));
        const auto manager = New<SceneManager>();
        manager->Deserialize(deserializer);
        EXPECT_FALSE(deserializer.HasErrors());
    }

    TEST(ChessSceneFilesTest, TheTopLevelNodesAreTheUiNodesThenThe32PiecesInTheDrawOrder) {
        pugi::xml_document document;
        ASSERT_TRUE(LoadShippedSceneDocument(document));
        const Vector<pugi::xml_node> nodes = NodesOf(SceneNodes(document));
        ASSERT_EQ(nodes.size(), kUiCount + kPieceCount);

        for (size_t index = 0; index < kUiCount; ++index) {
            EXPECT_STREQ(nodes[index].attribute("name").as_string(), kUiNodes[index].name) << index;
            EXPECT_EQ(nodes[index].attribute("layer").as_int(0), kUiNodes[index].layer) << kUiNodes[index].name;
        }
        for (size_t index = kUiCount; index < nodes.size(); ++index) {
            EXPECT_EQ(nodes[index].attribute("layer").as_int(0), 0) << nodes[index].attribute("name").as_string();
        }
    }

    TEST(ChessSceneFilesTest, EachPieceIsNamedAndSpelledAfterItsSquareInTheStartingPosition) {
        pugi::xml_document document;
        ASSERT_TRUE(LoadShippedSceneDocument(document));
        const Vector<pugi::xml_node> nodes = NodesOf(SceneNodes(document));
        ASSERT_EQ(nodes.size(), kUiCount + kPieceCount);

        // White back rank, white pawns, black pawns, black back rank: the order the scene draws them in
        constexpr int kRanksInOrder[] = {0, 1, kBoardSize - 2, kBoardSize - 1};
        size_t index = kUiCount;
        for (const int rank : kRanksInOrder) {
            for (int file = 0; file < kBoardSize; ++file, ++index) {
                const Square square{static_cast<int8_t>(file), static_cast<int8_t>(rank)};
                const pugi::xml_node node = nodes[index];
                const String name = ExpectedPieceName(square);
                SCOPED_TRACE(name.c_str());
                EXPECT_STREQ(node.attribute("name").as_string(), name.c_str());

                const pugi::xml_node piece = ComponentOf(node, "ChessPieceComponent");
                EXPECT_EQ(piece.attribute("file").as_int(-1), file);
                EXPECT_EQ(piece.attribute("rank").as_int(-1), rank);

                const pugi::xml_node sprite = ComponentOf(node, "SpriteRendererComponent");
                EXPECT_STREQ(sprite.attribute("spriteId").as_string(), ExpectedSpriteId(square).c_str());
                EXPECT_STREQ(sprite.attribute("fit").as_string(), "Contain");

                const pugi::xml_node transform = ComponentOf(node, "TransformComponent");
                EXPECT_FLOAT_EQ(transform.attribute("width").as_float(), kSquareSize);
                EXPECT_FLOAT_EQ(transform.attribute("height").as_float(), kSquareSize);
            }
        }
    }

    TEST(ChessSceneFilesTest, TheSceneFileHoldsOnlyReferencesAndEveryNodeFileIsReferenced) {
        pugi::xml_document raw;
        ASSERT_TRUE(raw.load_file(ShippedSceneConfig().string().c_str())) << ShippedSceneConfig().string();
        Set<String> referenced;
        for (const pugi::xml_node node : NodesOf(SceneNodes(raw))) {
            const pugi::xml_attribute ref = node.attribute("ref");
            EXPECT_TRUE(ref) << "an inline node came back: " << node.attribute("name").as_string();
            if (ref) {
                referenced.insert(String(ref.as_string()));
            }
        }

        const std::filesystem::path folder = ShippedConfigFolder() / "nodes";
        ASSERT_TRUE(std::filesystem::is_directory(folder)) << folder.string();
        size_t files = 0;
        //-NoStd: engine-file-layer: the test walks the source tree, which the engine's file layer does not mount
        for (const auto& entry : std::filesystem::directory_iterator(folder)) {
            if (entry.path().extension() != ".xml") {
                continue;
            }
            ++files;
            const String reference = Format("nodes/{}", entry.path().stem().string());
            EXPECT_TRUE(referenced.contains(reference)) << "not referenced by the scene: " << entry.path().filename().string();

            pugi::xml_document file;
            ASSERT_TRUE(file.load_file(entry.path().string().c_str())) << entry.path().string();
            const pugi::xml_node root = file.document_element();
            EXPECT_STREQ(root.name(), "node") << entry.path().filename().string();
            EXPECT_TRUE(root.attribute("contentVersion")) << entry.path().filename().string();
        }
        EXPECT_GT(files, 0u);
    }

}  // namespace BECore::Chess::Tests
