#include <BECore/Animation/TweenManager.h>
#include <BECore/UI/UIImageComponent.h>
#include <Chess/Components/PieceSprite.h>
#include <tests/ChessUiHarness.h>

/// The look the person picked (GraphicsChoice) reaching the scene: the pieces, the captured-material icons and the editor bar. The scene files name the Classic sprites, so a switch is
/// read back from the nodes the scene built, after the frames the components need.
namespace BECore::Chess::Tests {

    namespace {

        constexpr const char* kEditorBarNode = "EditorBar";
        constexpr double kStepSeconds = 0.25;         ///< longer than the slide of a move
        constexpr int kFramesToShowThePromotion = 3;  ///< one frame starts the slide, one ends it, one shows the new type

        PoolString SpriteName(Piece piece, GraphicsPreset preset) {
            return PieceSpriteId(piece, preset);
        }

    }  // namespace

    class ChessUiGraphicsTest : public ChessUiTest {
    protected:
        /// A frame with time in it: the slide of a played move runs on the tween manager, which the scene's update does not advance
        void StepGameTime() {
            CoreManager::GetTime().Advance(kStepSeconds);
            CoreManager::GetTweens().Advance(CoreManager::GetTime().GetDeltaTime());
            Frame();
        }

        /// A frame as the loop makes it: the update, then the draw (the material and the editor bar publish in either)
        void Frame() {
            _manager->UpdateAll();
            Draw();
        }

        /// Every piece node on the board shows the sprite of its piece in @p preset; the number of nodes checked comes back, so a scene with none cannot pass
        size_t CountPiecesShowing(GraphicsPreset preset) const {
            size_t checked = 0;
            for (const auto& node : GetScene().GetRootNode().GetChildren()) {
                const auto piece = node->GetComponent<ChessPieceComponent>();
                if (!piece) {
                    continue;
                }
                const Piece shown = Model().GetPieceOf(piece.Get());
                const auto sprite = node->GetComponent<SpriteRendererComponent>();
                EXPECT_TRUE(sprite);
                if (sprite) {
                    EXPECT_EQ(sprite->_spriteId, SpriteName(shown, preset)) << node->GetName().CStr();
                }
                ++checked;
            }
            return checked;
        }

        PoolString EditorIcon(const char* button) const {
            return ElementOf(kEditorBarNode, button).GetComponent<UIImageComponent>()->sprite;
        }
    };

    TEST_F(ChessUiGraphicsTest, ThePiecesStartAsClassicAndFollowTheChoiceThereAndBack) {
        Frame();
        EXPECT_EQ(CountPiecesShowing(GraphicsPreset::Classic), 32u);

        Graphics().Set(GraphicsPreset::Beer);
        Frame();
        EXPECT_EQ(CountPiecesShowing(GraphicsPreset::Beer), 32u);

        Graphics().Set(GraphicsPreset::Classic);
        Frame();
        EXPECT_EQ(CountPiecesShowing(GraphicsPreset::Classic), 32u);
    }

    TEST_F(ChessUiGraphicsTest, AResetKeepsTheLookAndTheGameStartKeepsNoOldChoice) {
        Graphics().Set(GraphicsPreset::Beer);
        Model().Reset();
        Frame();
        EXPECT_EQ(Graphics().Get(), GraphicsPreset::Beer) << "the look is the person's, not the game's";
        EXPECT_EQ(CountPiecesShowing(GraphicsPreset::Beer), 32u);

        ChessGame::GetActive()->Start();
        Frame();
        EXPECT_EQ(Graphics().Get(), GraphicsPreset::Classic);
        EXPECT_EQ(CountPiecesShowing(GraphicsPreset::Classic), 32u);
    }

    TEST_F(ChessUiGraphicsTest, APromotionUnderBeerShowsTheBeerQueenAndSwitchingBackTheClassicOne) {
        CoreManager::GetTime().Reset();  // the clock is the process's: a case before this one may have left a frame time in it
        CoreManager::GetTweens().Clear();
        StartFrom(kPromotionFen);
        const auto* node = PieceNodeAt("a7");
        ASSERT_NE(node, nullptr);
        const auto sprite = node->GetNode().GetComponent<SpriteRendererComponent>();
        ASSERT_TRUE(sprite);

        Graphics().Set(GraphicsPreset::Beer);
        Frame();
        EXPECT_EQ(sprite->_spriteId, "WhitePawn"_intern);

        ClickMove("a7", "a8");
        ASSERT_TRUE(ClickPromotion("PromoteQueen").Both());
        for (int frame = 0; frame < kFramesToShowThePromotion; ++frame) {
            StepGameTime();
        }
        Draw();
        EXPECT_EQ(sprite->_spriteId, "WhiteQueen"_intern) << "a promotion shows the type in the picked preset, not the scene's";

        Graphics().Set(GraphicsPreset::Classic);
        Frame();
        EXPECT_EQ(sprite->_spriteId, "ClassicWhiteQueen"_intern);
    }

    TEST_F(ChessUiGraphicsTest, TheCapturedMaterialIconsFollowTheChoice) {
        ASSERT_TRUE(PlayAll(Model(), {"e2e4", "d7d5", "e4d5"}));
        Frame();
        EXPECT_EQ(MaterialSprite("Bottom", "Pawn"), "ClassicBlackPawn");

        Graphics().Set(GraphicsPreset::Beer);
        Frame();
        EXPECT_EQ(MaterialSprite("Bottom", "Pawn"), "BlackPawn") << "no move was played: the revision of the choice alone republishes the icons";

        Graphics().Set(GraphicsPreset::Classic);
        Frame();
        EXPECT_EQ(MaterialSprite("Bottom", "Pawn"), "ClassicBlackPawn");
    }

    TEST_F(ChessUiGraphicsTest, TheEditorBarIconsFollowTheChoiceWhileTheBarIsShown) {
        ChessGame::GetActive()->BeginEditing(EditStart::Empty);
        Frame();
        EXPECT_EQ(EditorIcon("EditorBarQueen"), "ClassicWhiteQueen"_intern);

        Graphics().Set(GraphicsPreset::Beer);
        Frame();
        EXPECT_EQ(EditorIcon("EditorBarQueen"), "WhiteQueen"_intern);
        EXPECT_EQ(EditorIcon("EditorBarPawn"), "WhitePawn"_intern);

        Graphics().Set(GraphicsPreset::Classic);
        Frame();
        EXPECT_EQ(EditorIcon("EditorBarKing"), "ClassicWhiteKing"_intern);
    }

}  // namespace BECore::Chess::Tests
