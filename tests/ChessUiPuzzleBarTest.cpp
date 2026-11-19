#include <tests/ChessUiHarness.h>

/// The bar of the puzzle mode: the node PuzzleBar (config/nodes/ChessPuzzleBar.xml), shown exactly while a puzzle is on. The scene, the helpers and the fixture ChessUiTest are
/// tests/ChessUiHarness.h.
namespace BECore::Chess::Tests {

    namespace {

        constexpr const char* kBarNode = "PuzzleBar";
        constexpr const char* kBarRow = "PuzzleBarRow";

        constexpr const char* kBarButtons[] = {"PuzzleBarRetry", "PuzzleBarNext", "PuzzleBarLeave"};

        /// The controls of the HUD that stand beside the bar's place, and that the bar must never meet (Opening, the place itself, is empty while a puzzle is on)
        constexpr const char* kHudNeighbours[] = {"Turn", "Resign", "Menu", "Reset", "Status"};

    }  // namespace

    TEST_F(ChessUiTest, ThePuzzleBarPassesTheSizeMatrix) {
        Draw();
        auto canvas = CanvasOf(kBarNode);
        auto hud = Hud();
        ASSERT_TRUE(canvas);
        ASSERT_TRUE(hud);
        canvas->UpdateLayout(1280.0f, 720.0f);  // the reflected fields of the component reach the layout on its first update
        hud->UpdateLayout(1280.0f, 720.0f);
        UICanvasLayout& layout = canvas->GetLayout();
        UICanvasLayout& hudLayout = hud->GetLayout();
        UILayoutValidatorOptions options;
        options.minTouchSize = CoreManager::GetUIEnvironment().GetSettings().minTouchSize;

        String report;
        size_t reported = 0;
        const auto failures = ValidateOnMatrix(layout, options, ViewContext(), SceneViews());
        if (!failures.empty()) {
            reported++;
            report += DescribeMatrixFailures(failures).c_str();
        }
        // The bar is laid out beside the HUD in the same window, because the two are canvases of their own that nothing keeps apart but the places
        for (const UIMatrixCase& matrixCase : UILayoutMatrix()) {
            const UILayoutContext context = ContextOf(matrixCase, true);
            layout.Update(context);
            hudLayout.Update(context);
            String problems = BarProblems(layout, kBarButtons);
            for (const char* button : kBarButtons) {
                const UIElement* own = layout.root->FindByName(PoolString::Intern(button));
                for (const char* neighbour : kHudNeighbours) {
                    const UIElement* other = hudLayout.root->FindByName(PoolString::Intern(neighbour));
                    if (own && other && !own->rect.IsEmpty() && !other->rect.IsEmpty() && Touch(own->rect, other->rect, 0.0f)) {
                        problems += Format("{} meets the HUD's {}\n", button, neighbour);
                    }
                }
            }
            if (!problems.empty() && reported++ < kReportedCases) {
                report += Format("{}:\n{}", matrixCase.label.c_str(), problems.c_str());
            }
        }
        layout.Invalidate();
        hudLayout.Invalidate();
        EXPECT_TRUE(report.empty()) << report.c_str();
    }

}  // namespace BECore::Chess::Tests
