#include <BECore/UI/UIEnvironment.h>
#include <BECore/UI/UIToggleComponent.h>
#include <Chess/Components/ChessMovePanelComponent.h>
#include <Chess/Components/MoveRows.h>
#include <Chess/Game/MoveList.h>
#include <tests/ChessUiHarness.h>

/// The move list: the node MovePanel (config/nodes/ChessMovePanel.xml) and what ChessMovePanelComponent writes into it. The scene, the helpers and the fixture ChessUiTest are tests/ChessUiHarness.h.
namespace BECore::Chess::Tests {

    namespace {

        constexpr float kBandHeight = 56.0f;
        constexpr const char* kPanelBands[] = {"MovePanelBandTopPlayer", "MovePanelBandBottomPlayer", "MovePanelBandButtons"};  // in the order of the box, top down
        constexpr float kRowHeight = 56.0f;
        constexpr float kNumberWidth = 56.0f;
        constexpr const char* kMovePanelPrefix = "MovePanel";
        constexpr const char* kSides[] = {"White", "Black"};
        const Color kMovePanelPlateColor{32, 44, 50, 255};   // the plate of the box (ChessMovePanel.xml)
        const Color kMovePanelMarkColor{245, 205, 50, 150};  // the gold of the board's last-move squares, with the alpha of the mark
        constexpr size_t kLongGameRows = 20;
        constexpr const char* kBlackToMoveFen = "rnbqkbnr/pppppppp/8/8/4P3/8/PPPP1PPP/RNBQKBNR b KQkq e3 0 1";
        constexpr const char* kPawnOnTheSeventh = "k7/4P3/8/8/8/8/8/7K w - - 0 1";  // e7e8q is a check along the eighth rank

        /// Shows the box, every row and its marks (the component shows only what the game has), so the layout holds the panel at its full size
        void ShowEveryMovePanelRow(UICanvasComponent& canvas) {
            EXPECT_TRUE(canvas.SetElementVisible("MovePanelBox"_intern, true));
            for (size_t row = 0; row < kMovePanelRows; ++row) {
                EXPECT_TRUE(canvas.SetElementVisible(PoolString::Intern(Format("MovePanelRow{}", row).c_str()), true)) << row;
                for (const char* side : kSides) {
                    EXPECT_TRUE(canvas.SetElementVisible(PoolString::Intern(Format("MovePanel{}Mark{}", side, row).c_str()), true)) << side << row;
                }
            }
        }

        /// The own engine for the first @p answers analyses, then every analysis fails: a review that stops half way
        class AnalysesThenFails final : public IChessEngine {
        public:
            explicit AnalysesThenFails(int answers) : _answers(answers) {}

            std::expected<Move, EngineError> FindMove(const SearchRequest& request, const CancellationToken&) override {
                return FirstLegalMove(request);
            }

            std::expected<Analysis, EngineError> Analyse(const SearchRequest& request, const CancellationToken& token) override {
                if (_calls.fetch_add(1) >= _answers) {
                    return std::unexpected(EngineError{EngineErrorKind::ProcessDied, "the engine fell over"});
                }
                std::expected<ChessEnginePtr, EngineError> own = MakeAnalysisEngine(OpponentEngine::Own, {});
                if (!own) {
                    return std::unexpected(own.error());
                }
                return (*own)->Analyse(request, token);
            }

        private:
            int _answers;
            std::atomic<int> _calls{0};
        };

        constexpr size_t kRowsPerColumn = (kMovePanelRows + 1) / 2;  // the rows are two columns, of four and three: one above the other in the Wide view, side by side in the Tall one

        /// The view whose box a canvas laid out in this view shows: the Tall one has its own, every other view is asked for the Wide box (which none but Wide holds)
        MovePanelView PanelViewOf(const UICanvasLayout& layout) {
            return LaidOutForTall(layout) ? MovePanelView::Tall : MovePanelView::Wide;
        }

        /// MovePanelFits for the safe area of a laid out canvas, in the view it was laid out for
        bool PanelFitsIn(const UICanvasLayout& layout) {
            const Rect safe = layout.GetResolved().safeBounds;
            return MovePanelFits(safe.x, safe.y, safe.x + safe.w, safe.y + safe.h, PanelViewOf(layout));
        }

        /// The box of Game/MovePanel.h for the view of a laid out canvas
        Rect PanelBoxOf(const UICanvasLayout& layout) {
            if (PanelViewOf(layout) == MovePanelView::Tall) {
                return Rect{kMovePanelTallLeft, kMovePanelTallTop, kMovePanelTallRight - kMovePanelTallLeft, kMovePanelTallBottom - kMovePanelTallTop};
            }
            return Rect{kMovePanelLeft, kMovePanelTop, kMovePanelRight - kMovePanelLeft, kMovePanelBottom - kMovePanelTop};
        }

        /// What is wrong with the canvas MovePanel as it is laid out with the box and every row shown, one problem to a line, nothing when it is right: a name without the prefix, a box that is
        /// not the one of Game/MovePanel.h for its view, a box that lies inside the safe area exactly where MovePanelFits says it does not, bands and rows of the wrong height or place (the bands
        /// of the players are where the panel set of Material is drawn in the Wide view: kPanelTopBand, kPanelBottomBand; the Tall view hides them and puts the two columns of rows side by side),
        /// a word that a cell cuts
        String MovePanelProblems(const UICanvasLayout& layout) {
            String problems;
            const float scale = layout.GetResolved().scale;
            const Rect safe = layout.GetResolved().safeBounds;
            const bool tall = PanelViewOf(layout) == MovePanelView::Tall;

            Vector<PoolString> names;
            CollectNames(*layout.root, names);
            for (const PoolString& name : names) {
                if (name.ToStringView().substr(0, StringView(kMovePanelPrefix).size()) != kMovePanelPrefix) {
                    problems += Format("{} does not start with {}\n", name.ToStringView(), kMovePanelPrefix);
                }
            }

            const UIElement* box = layout.root->FindByName("MovePanelBox"_intern);
            if (box == nullptr || box->rect.IsEmpty()) {
                return problems + "MovePanelBox is missing or has an empty rect\n";
            }
            if (!SameRect(box->rect, PanelBoxOf(layout))) {
                problems += Format("MovePanelBox ({:.1f}, {:.1f}, {:.1f}, {:.1f}) is not the box of Game/MovePanel.h\n", box->rect.x, box->rect.y, box->rect.w, box->rect.h);
            }
            const bool fits = PanelFitsIn(layout);
            if (LiesIn(box->rect, safe) != fits) {
                problems += Format("MovePanelBox lies in the safe area ({:.1f}, {:.1f}, {:.1f}, {:.1f}): {}, the rule says {}\n", safe.x, safe.y, safe.w, safe.h, LiesIn(box->rect, safe), fits);
            }

            const auto find = [&](const String& name) -> const UIElement* {
                const UIElement* element = layout.root->FindByName(PoolString::Intern(name.c_str()));
                if (element == nullptr) {
                    problems += Format("{} is missing\n", name.c_str());
                } else if (element->rect.IsEmpty()) {
                    problems += Format("{} has an empty rect\n", name.c_str());
                    return nullptr;
                }
                return element;
            };
            const auto fitsIn = [&](const UIElement& element) {
                const UIContentFit fit = element.CheckContent(element.rect.w, element.rect.h, scale);
                if (fit.hasContent && !fit.fits) {
                    problems += Format("{} cuts '{}': {:.1f} x {:.1f} units at scale {:.3f}\n", element.name.ToStringView(), ShownText(element).c_str(), element.rect.w, element.rect.h, scale);
                }
            };

            const UIElement* list = find("MovePanelList");
            const UIElement* bands[std::size(kPanelBands)] = {};
            for (size_t band = 0; band < std::size(kPanelBands); ++band) {
                const bool playerBand = band + 1 < std::size(kPanelBands);
                if (tall && playerBand) {
                    const UIElement* hidden = layout.root->FindByName(PoolString::Intern(kPanelBands[band]));
                    if (hidden == nullptr || !hidden->rect.IsEmpty()) {
                        problems += Format("{} is not hidden in the Tall view, where the material keeps its rows above and below the board\n", kPanelBands[band]);
                    }
                    continue;
                }
                bands[band] = find(kPanelBands[band]);
                if (bands[band] != nullptr && (std::abs(bands[band]->rect.h - kBandHeight) > kPixelTolerance || !LiesIn(bands[band]->rect, box->rect))) {
                    problems += Format("{} is not a band of {:.0f} units inside the box\n", kPanelBands[band], kBandHeight);
                }
            }
            if (bands[0] != nullptr && (!LiesIn(bands[0]->rect, kPanelTopBand) || std::abs(bands[0]->rect.y - kPanelTopBand.y) > kPixelTolerance)) {
                problems += "MovePanelBandTopPlayer is not the top band, where the panel set of Material draws the upper group\n";
            }
            if (bands[1] != nullptr && (!LiesIn(bands[1]->rect, kPanelBottomBand) || std::abs(bands[1]->rect.y - kPanelBottomBand.y) > kPixelTolerance)) {
                problems += "MovePanelBandBottomPlayer is not the band where the panel set of Material draws the lower group\n";
            }
            if (list != nullptr && bands[0] != nullptr && bands[1] != nullptr && bands[2] != nullptr &&
                !(bands[0]->rect.y + bands[0]->rect.h <= list->rect.y && list->rect.y + list->rect.h <= bands[1]->rect.y && bands[1]->rect.y + bands[1]->rect.h <= bands[2]->rect.y)) {
                problems += "the box is not the top band, the list, the band of the other player and the band of the buttons, top down and apart\n";
            }
            if (tall && list != nullptr && bands[2] != nullptr && list->rect.y + list->rect.h > bands[2]->rect.y) {
                problems += "the box of the Tall view is not the list and the band of the buttons, top down and apart\n";
            }
            // The validator does not see a word cut by the air a button keeps inside its fill; Take back has only the rest of the band
            for (const char* name : {"MovePanelStepBack", "MovePanelStepForward", "MovePanelLatest", "MovePanelTakeBack"}) {
                const UIElement* button = find(name);
                if (button == nullptr) {
                    continue;
                }
                const UIContentFit fit = button->CheckContent(button->rect.w - 2.0f * kButtonAir, button->rect.h, scale);
                if (fit.hasContent && !fit.fits) {
                    problems += Format("{} cuts '{}': {:.1f} units wide at scale {:.3f}\n", name, ShownText(*button).c_str(), button->rect.w, scale);
                }
                if (bands[2] != nullptr && !LiesIn(button->rect, bands[2]->rect)) {
                    problems += Format("{} is not in the band of the buttons\n", name);
                }
            }
            for (size_t row = 0; row < kMovePanelRows; ++row) {
                const UIElement* line = find(Format("MovePanelRow{}", row));
                const UIElement* number = find(Format("MovePanelNumber{}", row));
                const UIElement* white = find(Format("MovePanelWhite{}", row));
                const UIElement* black = find(Format("MovePanelBlack{}", row));
                if (line == nullptr || number == nullptr || white == nullptr || black == nullptr) {
                    continue;
                }
                if (std::abs(line->rect.h - kRowHeight) > kPixelTolerance) {
                    problems += Format("{} is {:.1f} units high, not {:.0f}\n", line->name.ToStringView(), line->rect.h, kRowHeight);
                }
                // Its place in the list: the rows run down the first column, then the second, which is under the first in the Wide view and beside it in the Tall one
                if (list != nullptr) {
                    const size_t column = row / kRowsPerColumn;
                    const float width = tall ? list->rect.w * 0.5f : list->rect.w;
                    const size_t step = tall ? row % kRowsPerColumn : row;
                    const Rect place{list->rect.x + (tall ? static_cast<float>(column) * width : 0.0f), list->rect.y + static_cast<float>(step) * kRowHeight, width, kRowHeight};
                    if (!SameRect(line->rect, place)) {
                        problems += Format("{} ({:.1f}, {:.1f}, {:.1f}, {:.1f}) is not in its place of the list ({:.1f}, {:.1f}, {:.1f}, {:.1f})\n", line->name.ToStringView(), line->rect.x,
                                           line->rect.y, line->rect.w, line->rect.h, place.x, place.y, place.w, place.h);
                    }
                }
                if (std::abs(number->rect.w - kNumberWidth) > kPixelTolerance) {
                    problems += Format("{} is {:.1f} units wide, not {:.0f}\n", number->name.ToStringView(), number->rect.w, kNumberWidth);
                }
                if (std::abs(white->rect.w - black->rect.w) > kPixelTolerance || number->rect.x + number->rect.w > white->rect.x + kPixelTolerance ||
                    white->rect.x + white->rect.w > black->rect.x + kPixelTolerance) {
                    problems += Format("the cells of {} are not number, White, Black side by side, White and Black as wide as each other\n", line->name.ToStringView());
                }
                if (!LiesIn(black->rect, line->rect)) {
                    problems += Format("{} is not inside its row\n", black->name.ToStringView());
                }
                fitsIn(*number);
                for (const char* side : kSides) {
                    if (const UIElement* text = find(Format("MovePanel{}Text{}", side, row))) {
                        fitsIn(*text);
                    }
                    const UIElement* cell = StringView(side) == "White" ? white : black;
                    if (const UIElement* mark = find(Format("MovePanel{}Mark{}", side, row))) {
                        if (!SameRect(mark->rect, cell->rect)) {
                            problems += Format("{} does not fill the cell {} of the move it marks\n", mark->name.ToStringView(), cell->name.ToStringView());
                        }
                    }
                }
            }
            if (list != nullptr && !LiesIn(list->rect, box->rect)) {
                problems += "MovePanelList is not inside the box\n";
            }
            return problems;
        }

        /// Lays the canvas out in @p context the way the component leaves it: the box shown exactly where MovePanelFits says, every row shown; whether the box is shown
        bool LayOutAsTheComponentShowsIt(UICanvasComponent& canvas, const UILayoutContext& context) {
            const PoolString boxName = "MovePanelBox"_intern;
            UICanvasLayout& layout = canvas.GetLayout();
            canvas.SetElementVisible(boxName, true);
            layout.Invalidate();
            layout.Update(context);
            const bool fits = PanelFitsIn(layout);
            canvas.SetElementVisible(boxName, fits);
            layout.Invalidate();
            layout.Update(context);
            return fits;
        }

        /// The elements of the shipped canvas with the box and the rows shown, the text a copy of a test starts from; the variants keep what they hide
        String ShownMovePanelXml() {
            String xml = ShippedElementsXml("MovePanel");
            const String hidden = " visible=\"false\"";
            for (size_t at = xml.find(hidden); at != String::npos && at < xml.find("<variants>"); at = xml.find(hidden)) {
                xml.erase(at, hidden.size());
            }
            return xml;
        }

    }  // namespace

    TEST_F(ChessUiTest, TheMovePanelPassesTheSizeMatrixWithTheLongestMoves) {
        Draw();
        auto canvas = MovePanel();
        ASSERT_TRUE(canvas);
        canvas->UpdateLayout(1280.0f, 720.0f);  // the reflected fields of the component reach the layout on its first update
        ShowEveryMovePanelRow(*canvas);

        UICanvasLayout& layout = canvas->GetLayout();
        const PoolString boxName = "MovePanelBox"_intern;
        UILayoutValidatorOptions options;
        options.minTouchSize = CoreManager::GetUIEnvironment().GetSettings().minTouchSize;
        String report;
        size_t reported = 0;
        for (size_t round = 0; round < kMovePanelWordRounds; ++round) {
            PublishMovePanelRound(round);
            for (const UIMatrixCase& matrixCase : UILayoutMatrix()) {
                const UILayoutContext context = ContextOf(matrixCase, true);
                // The box shown whatever the window: its numbers, its words and the rule that says where it fits
                canvas->SetElementVisible(boxName, true);
                layout.Invalidate();
                layout.Update(context);
                String problems = MovePanelProblems(layout);

                // The box as the component shows it, exactly where the rule says: what is on the screen passes the validator
                canvas->SetElementVisible(boxName, PanelFitsIn(layout));
                layout.Invalidate();
                layout.Update(context);
                UILayoutValidatorOptions caseOptions = options;
                caseOptions.pixelDensity = context.pixelDensity;
                const auto issues = layout.Validate(caseOptions);
                if (!issues.empty()) {
                    problems += Format("the validator finds {} issue(s) with the box shown as the rule says\n", issues.size());
                }
                if (!problems.empty() && reported++ < kReportedCases) {
                    report += Format("round {}, {}:\n{}", round, matrixCase.label.c_str(), problems.c_str());
                }
            }
        }
        EXPECT_TRUE(report.empty()) << report.c_str();
        canvas->SetElementVisible(boxName, false);
        layout.Invalidate();
    }

    TEST_F(ChessUiTest, TheMovePanelIsHiddenInTheViewsThatHaveNoRoomForIt) {
        ASSERT_TRUE(Play(Model(), "e2e4")) << "a game with a move, so it is the window that hides the box";
        for (const Pair<float, float>& output : {Pair<float, float>{1024.0f, 768.0f}, Pair<float, float>{kNarrowestWidth, kNarrowestPortraitHeight}}) {
            SCOPED_TRACE(Format("output {}x{}", output.first, output.second).c_str());
            DrawAt(output.first, output.second);
            Settle();
            EXPECT_FALSE(MovePanelElement("MovePanelBox").active.visible);
            EXPECT_TRUE(MovePanelElement("MovePanelBox").rect.IsEmpty());
        }
    }

    TEST_F(ChessUiTest, TheMovePanelIsOnItsOwnLayerOverThePiecesAndUnderTheHud) {
        const auto& nodes = GetScene().GetRootNode().GetChildren();
        const auto indexOf = [&](const char* name) -> int {
            for (size_t i = 0; i < nodes.size(); ++i) {
                if (nodes[i]->GetName() == PoolString::Intern(name)) {
                    return static_cast<int>(i);
                }
            }
            return -1;
        };
        for (const char* name : {"Board", "BoardCoordinates", "Material", "MovePanel", "Result", "Hud", "MenuPanel", "OpponentPanel", "PromotionPanel"}) {
            ASSERT_GE(indexOf(name), 0) << "the scene has the node " << name;
        }
        const auto layerOf = [&](const char* name) { return nodes[static_cast<size_t>(indexOf(name))]->GetLayer(); };
        EXPECT_EQ(layerOf("MovePanel"), 4);
        EXPECT_LT(layerOf("MovePanel"), layerOf("Material")) << "under the material, which draws the players' rows over the bands of the box";
        EXPECT_GT(indexOf("MovePanel"), indexOf("Material")) << "written after the material, before the HUD";
        EXPECT_LT(indexOf("MovePanel"), indexOf("Hud"));
        EXPECT_GT(layerOf("MovePanel"), layerOf("Board"));
        EXPECT_GT(layerOf("MovePanel"), layerOf("Result"));
        EXPECT_LT(layerOf("MovePanel"), layerOf("Hud"));
        EXPECT_LT(layerOf("MovePanel"), layerOf("MenuPanel"));
        EXPECT_LT(layerOf("MovePanel"), layerOf("OpponentPanel"));
        EXPECT_LT(layerOf("MovePanel"), layerOf("PromotionPanel"));

        // The component comes before the canvas: it gives the variables their words before the canvas draws in the same frame
        const auto& components = nodes[static_cast<size_t>(indexOf("MovePanel"))]->GetComponents();
        const auto words = nodes[static_cast<size_t>(indexOf("MovePanel"))]->GetComponent<ChessMovePanelComponent>();
        const auto canvas = MovePanel();
        ASSERT_TRUE(words);
        ASSERT_TRUE(canvas);
        ASSERT_EQ(components.size(), 2u);
        EXPECT_EQ(components[0].Get(), static_cast<IComponent*>(words.Get()));
        EXPECT_EQ(components[1].Get(), static_cast<IComponent*>(canvas.Get()));

        EXPECT_EQ(canvas->_space, CanvasSpace::SceneView);
        EXPECT_FALSE(canvas->_useSafeArea) << "fixed scene units like the board; the rule of the panel reads the safe area itself";
        EXPECT_FALSE(canvas->_modal);
        EXPECT_TRUE(canvas->_visible);
    }

    TEST_F(ChessUiTest, TheMovePanelIsDrawnOverThePiecesAndTheResultBarAndUnderTheHudAndThePanels) {
        const auto king = GiveSpriteCell("WhiteKing_e1");
        StartFrom(kBlackMatesOnA1);
        ASSERT_TRUE(Play(Model(), "a8a1"));
        ASSERT_TRUE(Model().IsGameOver());
        _manager->UpdateAll();
        Settle();
        ASSERT_TRUE(OpenThePanel());
        Draw();

        const auto& calls = _renderer->calls;
        long lastSquare = -1;
        long firstPiece = -1;
        long dim = -1;
        long plateOfTheList = -1;
        long lastMark = -1;
        long resetButton = -1;
        long plateOfThePanel = -1;
        long firstChip = -1;
        for (size_t i = 0; i < calls.size(); ++i) {
            const DrawCall& call = calls[i];
            const long at = static_cast<long>(i);
            if (call.kind == DrawKind::Rect && (SameColor(call.color, kLightSquareColor) || SameColor(call.color, kDarkSquareColor))) {
                lastSquare = at;
            } else if (call.kind == DrawKind::Texture && call.texture == king.Get()) {
                firstPiece = firstPiece < 0 ? at : firstPiece;
            } else if (call.kind == DrawKind::Rect && SameColor(call.color, kBoardDimColor)) {
                dim = at;
            } else if (call.kind == DrawKind::Rect && SameColor(call.color, kMovePanelPlateColor)) {
                plateOfTheList = at;
            } else if (call.kind == DrawKind::Rect && SameColor(call.color, kMovePanelMarkColor)) {
                lastMark = at;
            } else if (call.kind == DrawKind::Rect && SameColor(call.color, kResetColor)) {
                resetButton = at;
            } else if (call.kind == DrawKind::Rect && SameColor(call.color, kPlateColor)) {
                plateOfThePanel = at;
            } else if (call.kind == DrawKind::Rect && SameColor(call.color, kMaterialChipColor)) {
                firstChip = firstChip < 0 ? at : firstChip;
            }
        }
        ASSERT_GE(lastSquare, 0);
        ASSERT_GE(firstPiece, 0) << "the king is drawn";
        ASSERT_GE(dim, 0) << "the game is over: the board is dimmed";
        ASSERT_GE(plateOfTheList, 0) << "the box is drawn";
        ASSERT_EQ(MaterialPlace(), "panel");
        ASSERT_GE(firstChip, 0) << "most of the pieces are gone: the material is drawn in the bands of the box";
        EXPECT_LT(plateOfTheList, firstChip) << "the chips of the material are drawn on the plate, not under it";
        ASSERT_GE(lastMark, 0) << "the move of the mate is marked";
        ASSERT_GE(resetButton, 0);
        ASSERT_GE(plateOfThePanel, 0) << "the opponent panel is open";
        EXPECT_LT(lastSquare, plateOfTheList) << "over the squares";
        EXPECT_LT(firstPiece, plateOfTheList) << "over the pieces";
        EXPECT_LT(dim, plateOfTheList) << "and over the dim of the finished game";
        EXPECT_LT(plateOfTheList, lastMark) << "the mark is on the plate";
        EXPECT_LT(lastMark, resetButton) << "under the HUD";
        EXPECT_LT(lastMark, plateOfThePanel) << "and under the panels";
    }

    TEST_F(ChessUiTest, OnlyTheCellsAndTheButtonsOfTheMovePanelTakeAClickAndTheBoardAndResetStillGetTheirOwn) {
        ASSERT_TRUE(PlayAll(Model(), {"e2e4", "e7e5"}));
        Settle();
        const auto canvas = MovePanel();
        ASSERT_TRUE(canvas);
        ASSERT_TRUE(MovePanelElement("MovePanelBox").active.visible);
        // The cells of the moves and the three step buttons of the band are buttons of ChessShowMove, Take back of ChessTakeBack, Hint of ChessHint, and the evaluation cell and the ratings cell
        // toggles of ChessSetEvaluation and ChessSetRatings; nothing else blocks or takes the pointer
        const PoolString takeBack = "MovePanelTakeBack"_intern;
        const PoolString hintNames[] = {"MovePanelHint"_intern, "MovePanelTallHint"_intern};
        const PoolString evalNames[] = {"MovePanelEval"_intern, "MovePanelTallEval"_intern};
        const PoolString ratingsNames[] = {"MovePanelRatings"_intern, "MovePanelTallRatings"_intern};
        Vector<String> clickable;
        for (const UIElement* element : AllElements(canvas->GetRoot())) {
            EXPECT_FALSE(element->blocksInput) << element->name.CStr();
            if (!element->IsInteractive()) {
                continue;
            }
            clickable.push_back(String(element->name.ToStringView()));
            const UIInputClicker* clicker = ClickerOf(*element);
            ASSERT_NE(clicker, nullptr) << element->name.CStr();
            if (element->name == takeBack) {
                EXPECT_EQ(clicker->action, "ChessTakeBack"_intern);
                continue;
            }
            if (eastl::find(eastl::begin(hintNames), eastl::end(hintNames), element->name) != eastl::end(hintNames)) {
                EXPECT_EQ(clicker->action, "ChessHint"_intern);
                continue;
            }
            if (eastl::find(eastl::begin(evalNames), eastl::end(evalNames), element->name) != eastl::end(evalNames)) {
                const UIToggleComponent* toggle = element->GetComponent<UIToggleComponent>();
                ASSERT_NE(toggle, nullptr) << element->name.CStr();
                EXPECT_EQ(toggle->action, "ChessSetEvaluation"_intern);
                EXPECT_EQ(toggle->bind, "analysisEvaluationOn"_intern);
                continue;
            }
            if (eastl::find(eastl::begin(ratingsNames), eastl::end(ratingsNames), element->name) != eastl::end(ratingsNames)) {
                const UIToggleComponent* toggle = element->GetComponent<UIToggleComponent>();
                ASSERT_NE(toggle, nullptr) << element->name.CStr();
                EXPECT_EQ(toggle->action, "ChessSetRatings"_intern);
                EXPECT_EQ(toggle->bind, "analysisRatingsOn"_intern);
                continue;
            }
            EXPECT_EQ(clicker->action, "ChessShowMove"_intern) << element->name.CStr();
            EXPECT_TRUE(ParseMoveListPress(clicker->value.ToStringView()).has_value()) << element->name.CStr();
        }
        EXPECT_EQ(clickable.size(), 2 * kMovePanelRows + 4 + 3 * std::size(hintNames))
            << "the cells, the four buttons, and Hint, the evaluation cell and the ratings cell of the Wide band and of the Tall twins";
        for (const char* name :
             {"MovePanelStepBack", "MovePanelStepForward", "MovePanelLatest", "MovePanelTakeBack", "MovePanelHint", "MovePanelEval", "MovePanelRatings", "MovePanelWhite6", "MovePanelBlack0"}) {
            EXPECT_NE(eastl::find(clickable.begin(), clickable.end(), String(name)), clickable.end()) << name;
        }
        for (const char* name : {"MovePanelBox", "MovePanelBandTopPlayer", "MovePanelBandBottomPlayer", "MovePanelNumber0"}) {
            const auto [x, y] = ScreenCenter(MovePanelElement(name));
            EXPECT_FALSE(CoreManager::GetUIInput().DispatchMouseButtonDown(Button(x, y, true))) << name;
            EXPECT_FALSE(CoreManager::GetUIInput().DispatchMouseButtonUp(Button(x, y, false))) << name;
        }
        EXPECT_FALSE(Model().IsViewing());

        // the board right next to the box takes a press, and Reset above it takes a click
        EXPECT_FALSE(Model().HasSelection());
        Click("h2");
        ASSERT_TRUE(Model().HasSelection()) << "the file h of the board is the one next to the box";
        EXPECT_EQ(Model().GetSelected(), Sq("h2"));
        ASSERT_TRUE(ClickButton(kResetColor)) << "Reset is at the right end above the box";
        Settle();
        EXPECT_TRUE(Model().GetMoves().empty());
        EXPECT_EQ(ShownMovePanelRows(), "");
    }

    TEST_F(ChessUiTest, TheShownMovePanelLiesInTheSafeAreaAndClearOfTheHudAndTheBoardOnTheSizeMatrix) {
        Draw();
        auto panel = MovePanel();
        auto hud = Hud();
        ASSERT_TRUE(panel);
        ASSERT_TRUE(hud);
        panel->UpdateLayout(1280.0f, 720.0f);
        hud->UpdateLayout(1280.0f, 720.0f);
        ShowEveryMovePanelRow(*panel);
        PublishMovePanelRound(0);

        size_t shown = 0;
        size_t shownTall = 0;
        size_t hidden = 0;
        String report;
        for (const UIMatrixCase& matrixCase : UILayoutMatrix()) {
            const UILayoutContext context = ContextOf(matrixCase, true);
            const bool isShown = LayOutAsTheComponentShowsIt(*panel, context);
            hud->GetLayout().Update(context);
            const UICanvasLayout& layout = panel->GetLayout();
            const UIElement* box = layout.root->FindByName("MovePanelBox"_intern);
            ASSERT_NE(box, nullptr);
            if (!isShown) {
                ++hidden;
                EXPECT_TRUE(box->rect.IsEmpty()) << matrixCase.label.c_str() << ": a hidden box takes no room";
                continue;
            }
            ++shown;
            const bool tall = LaidOutForTall(layout);
            shownTall += tall ? 1 : 0;
            if (!LaidOutForWide(layout) && !tall) {
                report += Format("{}: the box is shown in a view that is neither Wide nor Tall\n", matrixCase.label.c_str());
            }
            if (!LiesIn(box->rect, layout.GetResolved().safeBounds)) {
                report += Format("{}: the box is not inside the safe area\n", matrixCase.label.c_str());
            }
            // Right of the board in Wide; under the board and the lower row of the material (the margin set of Portrait) in Tall
            if (tall ? box->rect.y < kMaterialPortraitListBottom + kMinGap : box->rect.x < kBoardLeft + kBoardUnits) {
                report += Format("{}: the box reaches the board or the material\n", matrixCase.label.c_str());
            }
            // The insets move the HUD, not the box: a notch pushes the top row (Menu, Reset) down onto the top band. The home bar of the side insets lifts Status, which the Wide view
            // keeps low enough to stay clear of the band of the buttons. The rows are clear of the HUD in every case
            const UIElement* list = layout.root->FindByName("MovePanelList"_intern);
            ASSERT_NE(list, nullptr);
            String touching;
            for (const UIElement* control : AllElements(*hud->GetLayout().root)) {
                if (TextOf(*control) == nullptr || control->rect.IsEmpty()) {
                    continue;
                }
                if (Touch(list->rect, control->rect, kMinGap)) {
                    report += Format("{}: the rows and {} are closer than {:.0f} units\n", matrixCase.label.c_str(), control->name.ToStringView(), kMinGap);
                }
                if (Touch(box->rect, control->rect, kMinGap)) {
                    touching += Format(" {}", control->name.ToStringView());
                }
            }
            const bool notch = matrixCase.label.find("notch") != String::npos;
            // A notch moves only the top row, far above the box of the Tall view, and side insets hide that box
            const char* expected = !tall && notch ? " Menu Reset" : "";
            if (touching != expected) {
                report += Format("{}: the HUD controls closer to the box than {:.0f} units are '{}', not '{}'\n", matrixCase.label.c_str(), kMinGap, touching.c_str(), expected);
            }
            // Per band: a notch reaches the top band only; the band of the other player's material and the band of the buttons are clear in every window
            const char* expectedPerBand[] = {notch ? " Menu Reset" : "", "", ""};
            for (size_t band = 0; band < std::size(kPanelBands); ++band) {
                const UIElement* element = layout.root->FindByName(PoolString::Intern(kPanelBands[band]));
                ASSERT_NE(element, nullptr);
                if (element->rect.IsEmpty()) {
                    continue;  // the bands of the players in the Tall view
                }
                String near;
                for (const UIElement* control : AllElements(*hud->GetLayout().root)) {
                    if (TextOf(*control) != nullptr && !control->rect.IsEmpty() && Touch(element->rect, control->rect, kMinGap)) {
                        near += Format(" {}", control->name.ToStringView());
                    }
                }
                if (near != expectedPerBand[band]) {
                    report +=
                        Format("{}: the HUD controls closer to {} than {:.0f} units are '{}', not '{}'\n", matrixCase.label.c_str(), kPanelBands[band], kMinGap, near.c_str(), expectedPerBand[band]);
                }
            }
            // Not one unit of Status over the box, in any window: the line of a view is read beside the buttons that step it
            const UIElement* status = hud->GetLayout().root->FindByName("Status"_intern);
            ASSERT_NE(status, nullptr);
            if (Touch(box->rect, status->rect, 0.0f)) {
                report += Format("{}: Status overlaps the box of the move panel\n", matrixCase.label.c_str());
            }
        }
        panel->SetElementVisible("MovePanelBox"_intern, false);
        panel->GetLayout().Invalidate();
        hud->GetLayout().Invalidate();
        EXPECT_TRUE(report.empty()) << report.c_str();
        EXPECT_GT(shown, shownTall);
        EXPECT_GT(shownTall, 0u);
        EXPECT_GT(hidden, 0u) << "the default and the Portrait view never show it";
    }

    TEST_F(ChessUiTest, TheMovePanelBesideTheBoardIsShownOnlyWhereTheSideInsetsLeaveItsBox) {
        ASSERT_TRUE(PlayAll(Model(), {"e2e4"}));
        Settle();
        ASSERT_TRUE(MovePanelElement("MovePanelBox").active.visible) << "a Wide window with no insets";

        // 700 x 480 is the narrowest Wide window with side insets: they take more of each side of the view than the 40 units the box keeps from the edge
        ResizeWithSideInsets(kWideNarrowestWidth, kNarrowestHeight);
        EXPECT_FLOAT_EQ(GetScene().GetViewWidth(), kWideViewWidth);
        const Rect cut = MovePanel()->GetLayout().GetResolved().safeBounds;
        EXPECT_NEAR(cut.x, kViewShapes[2].NarrowestSafeLeft(), 0.2f);
        EXPECT_GT(cut.x, kWideViewWidth - kMovePanelRight);
        EXPECT_FALSE(MovePanelElement("MovePanelBox").active.visible);
        EXPECT_TRUE(MovePanelElement("MovePanelBox").rect.IsEmpty());
        EXPECT_EQ(MovePanelWord(0, "White"), "e4") << "the words are not taken away, only the box";

        // 3:2 and 16:10 at 480 px keep the box: the letterbox of the view is as wide as the inset, or the inset is less than the 40 units
        for (const Pair<float, float>& output : {Pair<float, float>{720.0f, 480.0f}, Pair<float, float>{768.0f, 480.0f}}) {
            SCOPED_TRACE(Format("output {}x{}", output.first, output.second).c_str());
            ResizeWithSideInsets(output.first, output.second);
            EXPECT_FLOAT_EQ(GetScene().GetViewWidth(), kWideViewWidth);
            const UIElement& box = MovePanelElement("MovePanelBox");
            EXPECT_TRUE(box.active.visible);
            EXPECT_TRUE(LiesIn(box.rect, MovePanel()->GetLayout().GetResolved().safeBounds));
        }

        ResizeWithSideInsets(kWideNarrowestWidth, kNarrowestHeight);
        ASSERT_FALSE(MovePanelElement("MovePanelBox").active.visible);
        ASSERT_TRUE(PlayAll(Model(), {"e7e5"}));
        Settle();
        EXPECT_FALSE(MovePanelElement("MovePanelBox").active.visible) << "it stays hidden when the game changes";
        ResizeWithoutInsets(1280.0f, 720.0f);
        EXPECT_TRUE(MovePanelElement("MovePanelBox").active.visible) << "and comes back when the window has room";
        EXPECT_EQ(ShownMovePanelRows(), "1. e4 [e5]");
    }

    TEST_F(ChessUiTest, InTheTallViewTheMovePanelIsUnderTheBoardInTwoColumnsAndTheMaterialKeepsItsRows) {
        ASSERT_TRUE(PlayAQuietGame(Model(), 9)) << "five rows, the last with White's move only: the fifth begins the second column";
        ResizeWithoutInsets(kNarrowestWidth, kNarrowestTallHeight);
        ASSERT_EQ(GetScene().GetActiveView().name, PoolString::Intern(kTallViewName));
        const UIElement& box = MovePanelElement("MovePanelBox");
        ASSERT_TRUE(box.active.visible);
        EXPECT_TRUE(SameRect(box.rect, Rect{kMovePanelTallLeft, kMovePanelTallTop, kMovePanelTallRight - kMovePanelTallLeft, kMovePanelTallBottom - kMovePanelTallTop}));
        EXPECT_TRUE(MovePanelElement("MovePanelBandTopPlayer").rect.IsEmpty());
        EXPECT_TRUE(MovePanelElement("MovePanelBandBottomPlayer").rect.IsEmpty());
        EXPECT_FALSE(MovePanelElement("MovePanelBandButtons").rect.IsEmpty()) << "the band of the buttons stays under the list";
        EXPECT_EQ(MaterialPlace(), "margin") << "above and below the board, as in Portrait";

        const Rect& first = MovePanelElement("MovePanelRow0").rect;
        const Rect& fifth = MovePanelElement("MovePanelRow4").rect;
        ASSERT_TRUE(MovePanelElement("MovePanelRow4").active.visible);
        EXPECT_FLOAT_EQ(fifth.y, first.y) << "the fifth row heads the second column";
        EXPECT_GE(fifth.x, first.x + first.w - kPixelTolerance);
        EXPECT_FALSE(MovePanelElement("MovePanelRow5").active.visible);
        EXPECT_EQ(MovePanelWord(4, "Black"), "") << "the fifth row has White's move only";
        EXPECT_FALSE(MovePanelWord(4, "White").empty());

        // A notch moves only the top of the safe area, far above the box; side insets take 64.8 units of each side of a view the width limits, more than the 40 the box keeps
        _renderer->width = kNarrowestWidth;
        _renderer->height = kNarrowestTallHeight;
        CoreManager::GetUIEnvironment().SetSafeAreaOverride(UIPreview::SimulatedSafeArea(SafeAreaSimulation::TopNotch, kNarrowestWidth, kNarrowestTallHeight));
        Settle();
        EXPECT_TRUE(MovePanelElement("MovePanelBox").active.visible) << "a notch";
        ResizeWithSideInsets(kNarrowestWidth, kNarrowestTallHeight);
        EXPECT_FALSE(MovePanelElement("MovePanelBox").active.visible) << "side insets";
        EXPECT_TRUE(MovePanelElement("MovePanelBox").rect.IsEmpty());

        // Back in a Wide window the two columns are one again, under each other
        ResizeWithoutInsets(1280.0f, 720.0f);
        ASSERT_TRUE(MovePanelElement("MovePanelBox").active.visible);
        EXPECT_FLOAT_EQ(MovePanelElement("MovePanelRow4").rect.y, MovePanelElement("MovePanelRow3").rect.y + kRowHeight);
        EXPECT_FLOAT_EQ(MovePanelElement("MovePanelRow4").rect.x, MovePanelElement("MovePanelRow0").rect.x);
        EXPECT_EQ(MaterialPlace(), "panel");
    }

    TEST_F(ChessUiTest, TheMovePanelIsHiddenUntilTheCanvasHasBeenLaidOutOnce) {
        // Before the first layout the safe area is not known and the box waits: nothing flashes. The first Draw() of a window that switches the view lays the canvas out in its second frame
        ASSERT_TRUE(MovePanel());
        EXPECT_FALSE(MovePanel()->GetLayout().HasLayout());
        EXPECT_FALSE(MovePanelElement("MovePanelBox").style.visible);
        Settle();
        EXPECT_TRUE(MovePanel()->GetLayout().HasLayout());
        EXPECT_TRUE(MovePanelElement("MovePanelBox").active.visible) << "a Wide window has room once the layout is known";
    }

    TEST_F(ChessUiTest, TheMovePanelStartsWithNoRow) {
        Settle();
        EXPECT_EQ(ShownMovePanelRows(), "");
        for (size_t row = 0; row < kMovePanelRows; ++row) {
            EXPECT_FALSE(MovePanelElement(Format("MovePanelRow{}", row).c_str()).active.visible) << row;
            for (const char* side : kSides) {
                EXPECT_FALSE(MovePanelElement(Format("MovePanel{}Mark{}", side, row).c_str()).active.visible) << side << row;
            }
        }
    }

    TEST_F(ChessUiTest, TheMovePanelShowsTheRowsOfTheGameAndMarksTheLastMoveOnly) {
        Settle();
        ASSERT_TRUE(PlayAll(Model(), {"e2e4"}));
        EXPECT_EQ(ShownMovePanelRows(), "1. [e4]") << "a row with White's move only";
        ASSERT_TRUE(PlayAll(Model(), {"e7e5"}));
        EXPECT_EQ(ShownMovePanelRows(), "1. e4 [e5]") << "after a move of Black the mark is on Black's cell";
        ASSERT_TRUE(PlayAll(Model(), {"g1f3"}));
        EXPECT_EQ(ShownMovePanelRows(), "1. e4 e5 | 2. [Nf3]");
        EXPECT_FALSE(MovePanelElement("MovePanelWhiteMark0").active.visible);
        EXPECT_FALSE(MovePanelElement("MovePanelBlackMark0").active.visible);
        EXPECT_FALSE(MovePanelElement("MovePanelBlackMark1").active.visible);
    }

    TEST_F(ChessUiTest, TheMovePanelShowsACaptureACastlingAPromotionACheckAndAMate) {
        Settle();
        ASSERT_TRUE(PlayAll(Model(), {"e2e4", "d7d5", "e4d5"}));
        EXPECT_EQ(MovePanelWord(1, "White"), "exd5");

        Model().Reset();
        ASSERT_TRUE(PlayAll(Model(), {"e2e4", "e7e5", "g1f3", "b8c6", "f1c4", "g8f6", "e1g1"}));
        EXPECT_EQ(ShownMovePanelRows(), "1. e4 e5 | 2. Nf3 Nc6 | 3. Bc4 Nf6 | 4. [O-O]");

        Model().SetPosition(PositionFromFen(kPawnOnTheSeventh));
        ASSERT_TRUE(PlayAll(Model(), {"e7e8q"}));
        EXPECT_EQ(MovePanelWord(0, "White"), "e8=Q+") << "a promotion that gives check";

        Model().Reset();
        ASSERT_TRUE(PlayAll(Model(), {"f2f3", "e7e5", "g2g4", "d8h4"}));
        ASSERT_TRUE(Model().IsGameOver());
        EXPECT_EQ(ShownMovePanelRows(), "1. f3 e5 | 2. g4 [Qh4#]");
    }

    TEST_F(ChessUiTest, AGameOfTwentyRowsShowsTheLastPageOfRows) {
        Settle();
        ASSERT_TRUE(PlayAQuietGame(Model(), 2 * kLongGameRows));
        const Vector<MoveListRow> list = BuildMoveList(Model().GetStartFen(), Model().GetMoves());
        ASSERT_EQ(list.size(), kLongGameRows);
        Draw();
        for (size_t row = 0; row < kMovePanelRows; ++row) {
            const MoveListRow& expected = list[kLongGameRows - kMovePanelRows + row];
            ASSERT_FALSE(expected.white.san.empty() || expected.black.san.empty());
            EXPECT_TRUE(MovePanelElement(Format("MovePanelRow{}", row).c_str()).active.visible) << row;
            EXPECT_EQ(ShownText(MovePanelElement(Format("MovePanelNumber{}", row).c_str())), Format("{}.", kLongGameRows - kMovePanelRows + 1 + row))
                << "the numbers are those of the game, not of the window";
            EXPECT_EQ(MovePanelWord(row, "White"), expected.white.san) << row;
            EXPECT_EQ(MovePanelWord(row, "Black"), expected.black.san) << row;
        }
        EXPECT_TRUE(MovePanelElement("MovePanelBlackMark6").active.visible) << "the last move of the game";
        EXPECT_FALSE(MovePanelElement("MovePanelWhiteMark6").active.visible);
        EXPECT_EQ(ShownText(MovePanelElement("MovePanelNumber0")), "14.");
        EXPECT_EQ(ShownText(MovePanelElement("MovePanelNumber6")), "20.");
    }

    TEST_F(ChessUiTest, AResetASetStateAndATakebackUpdateTheRowsAndTheMarkInTheSameFrame) {
        Settle();
        ASSERT_TRUE(PlayAll(Model(), {"e2e4", "e7e5", "g1f3"}));
        ASSERT_EQ(ShownMovePanelRows(), "1. e4 e5 | 2. [Nf3]");

        EXPECT_EQ(ChessGame::GetActiveModel()->TakeBack(1), 1u);
        EXPECT_EQ(ShownMovePanelRows(), "1. e4 [e5]");
        EXPECT_EQ(ChessGame::GetActiveModel()->TakeBack(2), 2u);
        EXPECT_EQ(ShownMovePanelRows(), "");

        ASSERT_TRUE(PlayAll(Model(), {"d2d4", "d7d5"}));
        ASSERT_EQ(ShownMovePanelRows(), "1. d4 [d5]");
        Model().Reset();
        EXPECT_EQ(ShownMovePanelRows(), "");
        EXPECT_FALSE(MovePanelElement("MovePanelBlackMark0").active.visible);

        ASSERT_TRUE(PlayAll(Model(), {"e2e4"}));
        ASSERT_TRUE(FenSetsTheState(kBlackToMoveFen)) << "set_state of the control API: the list restarts from that position";
        EXPECT_EQ(ShownMovePanelRows(), "");
        ASSERT_TRUE(PlayAll(Model(), {"e7e5"}));
        EXPECT_EQ(ShownMovePanelRows(), "1. ... [e5]") << "a game that begins with Black to move has no White move in its first row";
        EXPECT_FALSE(MovePanelElement("MovePanelWhiteMark0").active.visible) << "the dots are never the current move";
    }

    TEST_F(ChessUiTest, AMoveOfTheProgramIsSeenByTheMovePanelInTheSameFrameItIsPlayed) {
        StartFrom(kBlackCanTakeAPawn);
        Settle();
        ASSERT_EQ(ShownMovePanelRows(), "");
        const auto take = MoveGenerator::ParseUci(Model().GetPosition(), "d5e4");
        ASSERT_TRUE(take.has_value());
        const auto engine = New<GatedEngine>(true, *take);
        CountingFactory factory({engine});
        Opponent().SetEngineFactory(factory.AsFactory());
        ASSERT_TRUE(Opponent().SetSide(PieceColor::Black).has_value());
        engine->Open();
        ASSERT_TRUE(FramesUntil([&] { return Model().GetMoves().size() == 1; })) << "the program took the pawn";
        EXPECT_EQ(ShownMovePanelRows(), "2. ... [dxe4]");
    }

    TEST_F(ChessUiTest, ABrilliantMoveReadsWithTheSignOnlyWhileTheRatingsAreOn) {
        CountingAnalysisFactory factory({New<AnalysingEngine>(AnalysisScore{AnalysisScoreKind::Centipawns, 35}, Mv("e2e4"))});
        Analyst().SetEngineFactory(factory.AsFactory());
        ASSERT_TRUE(PlayAll(Model(), {"e2e4", "e7e5"}));
        ASSERT_EQ(ShownMovePanelRows(), "1. e4 [e5]") << "nothing is rated while the switch is off";

        Analyst().SetRatings(true);
        ASSERT_TRUE(WaitForRatings());
        EXPECT_EQ(ShownMovePanelRows(), "1. e4!! [e5]") << "e2e4 is the engine's move, e7e5 is not";
        EXPECT_EQ(MovePanelWord(0, "White"), "e4!!");

        Analyst().SetRatings(false);
        EXPECT_EQ(ShownMovePanelRows(), "1. e4 [e5]") << "off takes the sign back, the rows are rebuilt with no new version of the game";
    }

    TEST_F(ChessUiTest, TheRowsAreRebuiltWhenARatingArrivesWithNoNewVersionOfTheGame) {
        SKIP_WITHOUT_WORKER_THREADS();
        const auto engine = New<AnalysingEngine>(AnalysisScore{AnalysisScoreKind::Centipawns, 35}, Mv("e2e4"));
        engine->Gate(true);
        CountingAnalysisFactory factory({engine});
        Analyst().SetEngineFactory(factory.AsFactory());
        Analyst().SetRatings(true);
        ASSERT_TRUE(Play(Model(), "e2e4"));
        ASSERT_TRUE(FramesUntil([&] { return engine->Calls() == 1; }));
        ASSERT_EQ(ShownMovePanelRows(), "1. [e4]") << "the move is not rated yet";
        const uint64_t version = Model().GetVersion();

        engine->Open();
        ASSERT_TRUE(WaitForRatings());
        EXPECT_EQ(ShownMovePanelRows(), "1. [e4!!]");
        EXPECT_EQ(Model().GetVersion(), version) << "a rating is no change of the game";
    }

    TEST_F(ChessUiTest, APressOnAMarkedCellStillShowsThatMove) {
        CountingAnalysisFactory factory({New<AnalysingEngine>(AnalysisScore{AnalysisScoreKind::Centipawns, 35}, Mv("e2e4"))});
        Analyst().SetEngineFactory(factory.AsFactory());
        Analyst().SetRatings(true);
        ASSERT_TRUE(PlayAll(Model(), {"e2e4", "e7e5", "g1f3"}));
        ASSERT_TRUE(WaitForRatings());
        ASSERT_EQ(ShownMovePanelRows(), "1. e4!! e5 | 2. [Nf3]");

        PressWith("ChessShowMove", "white0");
        EXPECT_TRUE(Model().IsViewing());
        EXPECT_EQ(Model().GetShownMoveCount(), 1u);
        EXPECT_EQ(ShownMovePanelRows(), "1. [e4!!] e5 | 2. Nf3") << "the viewed move is the marked cell, with its sign";
    }

    // ---- The marks of a review: the fool's mate, where g2g4 is the blunder ----

    TEST(MoveRowsTest, TheMarkOfEachKindOfMoveIsThatOfItsQualityAndABrilliantPlyKeepsOnlyItsSign) {
        const auto reviewed = [](MoveQuality quality) { return Optional<ReviewedMove>(ReviewedMove{MoveReview{0, quality}, Move{}}); };
        const ReviewedMoves moves{reviewed(MoveQuality::Best),    reviewed(MoveQuality::Good),    reviewed(MoveQuality::Inaccuracy),
                                  reviewed(MoveQuality::Mistake), reviewed(MoveQuality::Blunder), std::nullopt,
                                  reviewed(MoveQuality::Blunder), reviewed(MoveQuality::Mistake)};
        const Vector<bool> brilliant{false, false, false, false, false, false, true};

        const Vector<MovePanelMistake> marks = ReviewMistakesByPly(moves, brilliant);

        ASSERT_EQ(marks.size(), moves.size());
        EXPECT_EQ(marks[0], MovePanelMistake::None);
        EXPECT_EQ(marks[1], MovePanelMistake::None);
        EXPECT_EQ(marks[2], MovePanelMistake::Inaccuracy);
        EXPECT_EQ(marks[3], MovePanelMistake::Mistake);
        EXPECT_EQ(marks[4], MovePanelMistake::Blunder);
        EXPECT_EQ(marks[5], MovePanelMistake::None) << "a move not reviewed yet";
        EXPECT_EQ(marks[6], MovePanelMistake::None) << "flagged brilliant by the ratings: only the `!!` stays";
        EXPECT_EQ(marks[7], MovePanelMistake::Mistake) << "the flags are shorter than the moves";
        EXPECT_TRUE(ReviewMistakesByPly({}, brilliant).empty());
    }

    TEST_F(ChessUiTest, AReviewMarksTheMistakesInTheListAsTheyComeAndAllOfThemWhenItIsDone) {
        SKIP_WITHOUT_WORKER_THREADS();
        ReviewOnTheOwnEngine();
        ASSERT_TRUE(PlayFoolsMate());
        ASSERT_EQ(ShownMovePanelRows(), "1. f3 e5 | 2. g4 [Qh4#]") << "no review: no mark";

        ASSERT_TRUE(Analyst().StartReview(Model()).has_value());
        bool sawItRunWithTheBlunderUnknown = false;
        ASSERT_TRUE(FramesUntil([&] {
            const ReviewedMoves reviewed = Analyst().GetReviewedMoves(Model());
            const String word = MovePanelWord(1, "White");
            if (reviewed.size() == 4 && !reviewed[2]) {
                EXPECT_EQ(word, "g4") << "a move that is not reviewed is not marked";
                sawItRunWithTheBlunderUnknown = true;
            } else if (reviewed.size() == 4) {
                EXPECT_EQ(word, "g4??") << "the review has the blunder";
            }
            return Analyst().GetReviewState(Model()) == ReviewState::Done;
        }));

        EXPECT_TRUE(sawItRunWithTheBlunderUnknown) << "a position arrives per frame, so the blunder is not known at the first one";
        EXPECT_EQ(MovePanelWord(1, "White"), "g4??");
        EXPECT_EQ(MovePanelWord(0, "Black"), "e5") << "e7e5 is no mistake";
        EXPECT_EQ(MovePanelWord(1, "Black"), "Qh4#") << "the mate is the best move";
        EXPECT_EQ(ShownMovePanelRows(), "1. f3? e5 | 2. g4?? [Qh4#]");
    }

    TEST_F(ChessUiTest, ATakebackAndAResetTakeTheMarksOffWithNoFrameBetween) {
        SKIP_WITHOUT_WORKER_THREADS();
        ReviewOnTheOwnEngine();
        ASSERT_TRUE(PlayFoolsMate());
        ASSERT_TRUE(Analyst().StartReview(Model()).has_value());
        ASSERT_TRUE(WaitForReview());
        ASSERT_EQ(MovePanelWord(1, "White"), "g4??");

        ASSERT_EQ(Model().TakeBack(1), 1u);
        EXPECT_EQ(Analyst().GetReviewState(Model()), ReviewState::Idle) << "read before any frame";
        EXPECT_EQ(CurrentMovePanelRows(&Model(), &Analyst())[1].white.text, "g4");
        EXPECT_EQ(ShownMovePanelRows(), "1. f3 e5 | 2. [g4]") << "the game is on again, the list shows no mark";

        FrameOfGameTime(0.0);  // the analyst's Update() drops the review for good; before it the same game played again would still be the held one
        ASSERT_TRUE(Play(Model(), "d8h4"));
        EXPECT_EQ(ShownMovePanelRows(), "1. f3 e5 | 2. g4 [Qh4#]") << "the old review is gone, nothing marks the replayed game";
    }

    TEST_F(ChessUiTest, AResetTakesTheMarksOffWithNoFrameBetween) {
        SKIP_WITHOUT_WORKER_THREADS();
        ReviewOnTheOwnEngine();
        ASSERT_TRUE(PlayFoolsMate());
        ASSERT_TRUE(Analyst().StartReview(Model()).has_value());
        ASSERT_TRUE(WaitForReview());

        Model().Reset();

        EXPECT_EQ(Analyst().GetReviewState(Model()), ReviewState::Idle);
        EXPECT_TRUE(Analyst().GetReviewedMoves(Model()).empty());
        EXPECT_EQ(ShownMovePanelRows(), "");
    }

    TEST_F(ChessUiTest, AbandoningTheReviewTakesTheMarksOff) {
        SKIP_WITHOUT_WORKER_THREADS();
        ReviewOnTheOwnEngine();
        ASSERT_TRUE(PlayFoolsMate());
        ASSERT_TRUE(Analyst().StartReview(Model()).has_value());
        ASSERT_TRUE(WaitForReview());
        ASSERT_EQ(MovePanelWord(1, "White"), "g4??");

        Analyst().AbandonReview();

        EXPECT_EQ(MovePanelWord(1, "White"), "g4");
    }

    TEST_F(ChessUiTest, AFailedReviewKeepsTheMarksOfTheMovesThatCame) {
        SKIP_WITHOUT_WORKER_THREADS();
        // f3 e5 g4 a6: Black misses the mate, White resigns. The engine answers four positions and fails on the fifth, so g4 is reviewed and a6 is not
        ASSERT_TRUE(PlayAll(Model(), {"f2f3", "e7e5", "g2g4", "a7a6"}));
        Model().Resign(PieceColor::White);
        ASSERT_TRUE(Model().IsGameOver());
        constexpr int kAnswers = 4;
        CountingAnalysisFactory factory({New<AnalysesThenFails>(kAnswers)});
        Analyst().SetEngineFactory(factory.AsFactory());

        ASSERT_TRUE(Analyst().StartReview(Model()).has_value());
        ASSERT_TRUE(FramesUntil([&] { return Analyst().GetReviewState(Model()) == ReviewState::Failed; }));

        EXPECT_EQ(MovePanelWord(1, "White"), "g4??") << "the blunder came before the failure";
        EXPECT_EQ(MovePanelWord(1, "Black"), "a6") << "the last move has no review, so no mark";
        EXPECT_FALSE(Analyst().GetReviewedMoves(Model())[3].has_value());
    }

    TEST_F(ChessUiTest, TheRatingsSignAndTheReviewMarksStandOnDifferentCells) {
        // The double answers e2e4 at +35 everywhere: e2e4 is the engine's move (brilliant for the ratings, the best move for the review), e7e5 costs 70 centipawns (an inaccuracy)
        CountingAnalysisFactory factory({New<AnalysingEngine>(AnalysisScore{AnalysisScoreKind::Centipawns, 35}, Mv("e2e4"))});
        Analyst().SetEngineFactory(factory.AsFactory());
        ASSERT_TRUE(PlayAll(Model(), {"e2e4", "e7e5"}));
        Model().Resign(PieceColor::White);
        Analyst().SetRatings(true);
        ASSERT_TRUE(WaitForRatings());
        ASSERT_EQ(ShownMovePanelRows(), "1. e4!! [e5]");

        ASSERT_TRUE(Analyst().StartReview(Model()).has_value());
        ASSERT_TRUE(WaitForReview());

        EXPECT_EQ(ShownMovePanelRows(), "1. e4!! [e5?!]");
    }

    TEST_F(ChessUiTest, WithNoGameActiveTheMovePanelHasNoRowShown) {
        ASSERT_TRUE(PlayAll(Model(), {"e2e4", "e7e5"}));
        ASSERT_EQ(ShownMovePanelRows(), "1. e4 [e5]");

        GameManager::GetInstance().Shutdown();
        ASSERT_EQ(ChessGame::GetActive(), nullptr);
        Draw();
        Draw();
        EXPECT_EQ(ShownMovePanelRows(), "");
        EXPECT_EQ(MovePanelWord(0, "White"), "");
        EXPECT_EQ(MovePanelWord(0, "Black"), "");
        EXPECT_FALSE(MovePanelElement("MovePanelBlackMark0").active.visible);
    }

    TEST_F(ChessUiTest, ABrokenCopyOfTheMovePanelCanvasIsCaughtByTheChecks) {
        Draw();
        const String shipped = ShownMovePanelXml();
        ASSERT_FALSE(shipped.empty());
        const auto problemsOf = [&](const String& xml) {
            CanvasCopy copy(xml, false);
            String report;
            size_t reported = 0;
            PublishMovePanelRound(0);
            OnEveryCaseOfTheMatrix(
                copy.layout,
                [&](const String& label, const UICanvasLayout& laidOut) {
                    const String problems = MovePanelProblems(laidOut);
                    if (!problems.empty() && reported++ < kReportedCases) {
                        report += Format("{}:\n{}", label.c_str(), problems.c_str());
                    }
                },
                true);
            return report;
        };

        EXPECT_EQ(problemsOf(shipped), "") << "the same checks pass on the shipped canvas";

        String noMark = shipped;
        ASSERT_NO_FATAL_FAILURE(Replace(noMark, R"(name="MovePanelBlackMark3")", R"(name="MovePanelBlackMarkX3")"));
        EXPECT_NE(problemsOf(noMark).find("MovePanelBlackMark3 is missing"), String::npos) << "a row without its mark";

        String unprefixed = shipped;
        ASSERT_NO_FATAL_FAILURE(Replace(unprefixed, R"(name="MovePanelBandButtons")", R"(name="BandButtons")"));
        EXPECT_NE(problemsOf(unprefixed).find("BandButtons does not start with MovePanel"), String::npos) << "elements are found by name in every canvas";

        String swapped = shipped;
        ASSERT_NO_FATAL_FAILURE(Replace(swapped, R"(name="MovePanelBandBottomPlayer")", R"(name="MovePanelBandSwap")"));
        ASSERT_NO_FATAL_FAILURE(Replace(swapped, R"(name="MovePanelBandButtons")", R"(name="MovePanelBandBottomPlayer")"));
        ASSERT_NO_FATAL_FAILURE(Replace(swapped, R"(name="MovePanelBandSwap")", R"(name="MovePanelBandButtons")"));
        EXPECT_NE(problemsOf(swapped).find("MovePanelBandBottomPlayer is not the band where the panel set of Material draws"), String::npos) << "the bands under the list in the other order";

        String bigFont = shipped;
        ASSERT_NO_FATAL_FAILURE(Replace(bigFont, R"(name="MovePanelWhiteText0" anchor="Stretch" fontSize="24")", R"(name="MovePanelWhiteText0" anchor="Stretch" fontSize="90")"));
        EXPECT_NE(problemsOf(bigFont).find("MovePanelWhiteText0 cuts"), String::npos) << "a font of 90 does not fit the cell";

        String lowRow = shipped;
        ASSERT_NO_FATAL_FAILURE(Replace(lowRow, R"(name="MovePanelRow0" height="56")", R"(name="MovePanelRow0" height="40")"));
        EXPECT_NE(problemsOf(lowRow).find("MovePanelRow0 is 40.0 units high"), String::npos);

        String moved = shipped;
        ASSERT_NO_FATAL_FAILURE(Replace(moved, R"(name="MovePanelBox" x="760" y="80")", R"(name="MovePanelBox" x="700" y="80")"));
        EXPECT_NE(problemsOf(moved).find("is not the box of Game/MovePanel.h"), String::npos);

        String movedTall = shipped;
        ASSERT_NO_FATAL_FAILURE(Replace(movedTall, R"(element="MovePanelBox" x="40" y="840")", R"(element="MovePanelBox" x="40" y="800")"));
        EXPECT_NE(problemsOf(movedTall).find("9:16 480x853 no insets:\nMovePanelBox (40.0, 800.0"), String::npos) << "the box of the Tall view onto the material";

        String oneColumn = shipped;
        ASSERT_NO_FATAL_FAILURE(Replace(oneColumn, R"(element="MovePanelList" height="224" direction="Horizontal")", R"(element="MovePanelList" height="224" direction="Vertical")"));
        EXPECT_NE(problemsOf(oneColumn).find("MovePanelRow4 ("), String::npos) << "the columns of the Tall view under each other";
    }

}  // namespace BECore::Chess::Tests
