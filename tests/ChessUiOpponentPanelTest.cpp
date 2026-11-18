#include <tests/ChessUiHarness.h>

/// The opponent panel: its layout, its tables, broken copies of it, its words and actions, and clicks where it is drawn. The scene, the helpers and the fixture ChessUiTest are tests/ChessUiHarness.h.
namespace BECore::Chess::Tests {

    // ---- the opponent panel: the layout ------------------------------------------------------------------------------------------
    // The panel is the node OpponentPanel of the shipped scene (a modal canvas, hidden at the start). The checks follow engine/tests/BECore/UISelectorScreenTest.cpp, the template of
    // a choice panel: the validator on the size matrix, then CheckContent() of every control (an ellipsis label can shrink, so the validator alone misses a cut word), once per
    // case of the matrix and for the narrowest window alone, with every word the game can show

    TEST_F(ChessUiTest, ThePanelPassesTheSizeMatrix) {
        auto panel = Panel();
        ASSERT_TRUE(panel) << "the shipped scene has the node OpponentPanel";
        panel->UpdateLayout(1280.0f, 720.0f);  // the reflected fields of the component reach the layout on its first update
        const auto failures = ValidateWithWords(panel->GetLayout());
        EXPECT_TRUE(failures.empty()) << DescribeMatrixFailures(failures).c_str();
    }

    TEST_F(ChessUiTest, NoWordOfThePanelIsCutOnAnyCaseOfTheMatrix) {
        auto panel = Panel();
        ASSERT_TRUE(panel);
        panel->UpdateLayout(1280.0f, 720.0f);
        const String problems = PanelProblemsOnMatrix(panel->GetLayout());
        EXPECT_TRUE(problems.empty()) << problems.c_str();
    }

    TEST_F(ChessUiTest, NoWordOfThePanelIsCutInTheNarrowestWindowAndEverySegmentIsATouchTarget) {
        auto panel = Panel();
        ASSERT_TRUE(panel);
        panel->UpdateLayout(1280.0f, 720.0f);
        UICanvasLayout& layout = panel->GetLayout();
        const HudWords words = AllHudWords();
        const float minTouch = CoreManager::GetUIEnvironment().GetSettings().minTouchSize;

        for (const ViewShape& shape : kViewShapes) {
            SCOPED_TRACE(shape.name);
            LayOutInTheNarrowestWindow(layout, shape);

            const float scale = shape.NarrowestScale();
            EXPECT_NEAR(layout.GetResolved().scale, scale, 0.01f);
            EXPECT_NEAR(layout.root->rect.w, shape.NarrowestSafeWidth(), 0.5f) << "the safe area, in view units: about 689 and 590";

            for (size_t round = 0; round < words.Rounds(); ++round) {
                words.Publish(round);
                const String problems = PanelProblems(layout);
                EXPECT_TRUE(problems.empty()) << "round " << round << ":\n" << problems.c_str();
            }

            size_t segments = 0;
            for (const PanelSegment& segment : ExpectedPanelSegments()) {
                const UIElement& button = PanelButton(segment.name.c_str());
                ++segments;
                EXPECT_GE(button.rect.h, kSegmentHeight - kPixelTolerance) << segment.name.c_str();
                EXPECT_GE(std::min(button.rect.w, button.rect.h) * layout.GetResolved().scale, minTouch - kPixelTolerance) << segment.name.c_str() << " is a touch target on the smallest window";
                const UIContentFit fit = button.CheckContent(button.rect.w - 2.0f * kButtonAir, button.rect.h, layout.GetResolved().scale);
                EXPECT_TRUE(fit.hasContent && fit.fits) << segment.name.c_str() << " '" << ShownText(button).c_str() << "' in " << button.rect.w << " units";
            }
            EXPECT_EQ(segments, kPanelSegments) << "two + two + two + four";
        }
        layout.Invalidate();
    }

    TEST_F(ChessUiTest, EveryErrorSentenceFitsTheErrorLineOfThePanelInTheNarrowestWindow) {
        auto panel = Panel();
        ASSERT_TRUE(panel);
        panel->UpdateLayout(1280.0f, 720.0f);
        UICanvasLayout& layout = panel->GetLayout();

        size_t sentences = 0;
        for (const ViewShape& shape : kViewShapes) {
            SCOPED_TRACE(shape.name);
            LayOutInTheNarrowestWindow(layout, shape);
            const UIElement& line = PanelLabel("OpponentError");
            ASSERT_FALSE(line.rect.IsEmpty());
            const float scale = layout.GetResolved().scale;

            for (const EngineErrorKind kind : EnumUtils<EngineErrorKind>::Values()) {
                const String sentence = OpponentText::Error(EngineError{kind, {}});
                CoreManager::GetUIVars().Set("opponentError"_intern, StringView(sentence));
                const UIContentFit fit = line.CheckContent(line.rect.w, line.rect.h, scale);
                EXPECT_TRUE(fit.hasContent && fit.fits) << "'" << sentence.c_str() << "' in " << line.rect.w << " units";
                ++sentences;
            }
        }
        EXPECT_GE(sentences, 2u);
        layout.Invalidate();
    }

    TEST_F(ChessUiTest, TheRowsAreLaidOutAsTheCommentOfTheSceneSays) {
        ASSERT_TRUE(OpenThePanel());
        Draw();
        const UIElement& plate = PanelControl<UIStack>("Plate");
        EXPECT_LE(plate.rect.w, 640.0f + kPixelTolerance);
        EXPECT_NEAR(plate.rect.h, 460.0f, kPixelTolerance) << "16 + 44 + 4 x 56 + 32 + 56 + 6 x 12 + 16";
        for (const char* row : {"Mode", "Color", "Engine", "Level"}) {
            const UIElement& caption = PanelElement(Format("{}Caption", row).c_str());
            const UIElement& group = PanelElement(Format("{}Group", row).c_str());
            EXPECT_LT(caption.rect.x + caption.rect.w, group.rect.x + kPixelTolerance) << row << ": the caption is beside its group";
            EXPECT_NEAR(caption.rect.y, group.rect.y, kPixelTolerance) << row;
            EXPECT_NEAR(group.rect.h, kSegmentHeight, kPixelTolerance) << row;
        }
        const UIElement& level = PanelControl<UISegmented>("LevelGroup");
        float left = level.rect.x;
        float width = -1.0f;
        for (const PanelSegment& segment : ExpectedPanelSegments()) {
            if (StringView(segment.group) != "LevelGroup") {
                continue;
            }
            const UIElement& button = PanelButton(segment.name.c_str());
            EXPECT_NEAR(button.rect.x, left, kPixelTolerance) << segment.name.c_str();
            width = width < 0.0f ? button.rect.w : width;
            EXPECT_NEAR(button.rect.w, width, kPixelTolerance) << segment.name.c_str() << ": equal shares by flex";
            left = button.rect.x + button.rect.w + 4.0f;
        }
        EXPECT_NEAR(left - 4.0f, level.rect.x + level.rect.w, kPixelTolerance) << "the four segments fill the group";
    }

    // ---- the opponent panel: the words and the ids come from the tables ------------------------------------------------------------

    TEST_F(ChessUiTest, TheTextAndTheValueOfEverySegmentAreThoseOfTheTables) {
        const Vector<PanelSegment> expected = ExpectedPanelSegments();
        ASSERT_EQ(expected.size(), kPanelSegments);

        for (const PanelSegment& want : expected) {
            SCOPED_TRACE(want.name.c_str());
            const UIElement& button = PanelButton(want.name.c_str());
            EXPECT_EQ(ShownText(button), want.word) << "the word on the segment is the one of the table";
            const StringView value = ClickerOf(button)->value.ToStringView();
            EXPECT_EQ(String(value.data(), value.size()), want.id);
            EXPECT_EQ(ClickerOf(button)->action, PoolString::Intern(want.action));
            EXPECT_EQ(button.selected, false) << "nothing is selected before a frame";
        }

        // The id of a segment is a valid id of the table behind its group, and the group holds exactly the segments of the table (a new level or engine without a segment fails here)
        for (const char* groupName : {"ModeGroup", "ColorGroup", "EngineGroup", "LevelGroup"}) {
            const UISegmented& group = PanelControl<UISegmented>(groupName);
            size_t inTable = 0;
            for (const PanelSegment& want : expected) {
                inTable += StringView(want.group) == groupName ? 1u : 0u;
            }
            size_t buttons = 0;
            for (const auto& child : group.children) {
                const UIInputClicker* button = ClickerOf(*child);
                ASSERT_NE(button, nullptr) << groupName << ": only segments";
                ++buttons;
                const StringView value = button->value.ToStringView();
                const StringView groupId = groupName;
                if (groupId == "ModeGroup") {
                    EXPECT_TRUE(ParseMode(value).has_value()) << value.data();
                } else if (groupId == "ColorGroup") {
                    EXPECT_TRUE(ParseColor(value).has_value()) << value.data();
                } else if (groupId == "EngineGroup") {
                    EXPECT_TRUE(FindOpponentEngine(value).has_value()) << value.data();
                } else {
                    EXPECT_TRUE(FindOpponentLevel(value).has_value()) << value.data();
                }
            }
            EXPECT_EQ(buttons, inTable) << groupName;
        }
    }

    TEST_F(ChessUiTest, TheGroupsAreBoundToTheVariablesTheBoardPublishesAndTheirNamesDoNotCollideWithTheHud) {
        const struct {
            const char* group;
            const char* variable;
        } bound[] = {{"ModeGroup", "opponentMode"}, {"ColorGroup", "opponentColor"}, {"EngineGroup", "opponentEngine"}, {"LevelGroup", "opponentLevel"}};
        for (const auto& [group, variable] : bound) {
            EXPECT_EQ(PanelControl<UISegmented>(group).bind, PoolString::Intern(variable)) << group;
        }
        // Elements are found by name in every canvas of the scene: the panel must not reuse a name of the HUD (nor the old Level and Engine)
        for (const char* name : {"Turn", "Menu", "Reset", "Side", "Engine", "Status", "Level"}) {
            const auto hudHas = HudElement(name) != nullptr;
            const auto panelHas = Panel()->GetRoot().FindByName(PoolString::Intern(name)) != nullptr;
            EXPECT_FALSE(hudHas && panelHas) << name << " is in both canvases";
        }
        EXPECT_EQ(Panel()->GetRoot().FindByName("Level"_intern), nullptr);
        EXPECT_EQ(Panel()->GetRoot().FindByName("Engine"_intern), nullptr);
        EXPECT_EQ(Panel()->GetRoot().FindByName("Mode"_intern), nullptr);
    }

    TEST_F(ChessUiTest, EveryActionOfThePanelIsOneTheBoardAnswers) {
        // Press each action with the id of a segment, as the segment would: the controller reads the pick back (the unknown action is ignored by ChessOpponentPanelComponent)
        CountingFactory factory({New<InstantEngine>()});
        Opponent().SetEngineFactory(factory.AsFactory());
        _manager->UpdateAll();
        const struct {
            const char* action;
            const char* value;
        } picks[] = {{kSetEngineAction, "own"}, {kSetLevelAction, "max"}};
        for (const auto& [action, value] : picks) {
            PressWith(action, value);
        }
        EXPECT_EQ(Opponent().GetEngine(), OpponentEngine::Own);
        EXPECT_EQ(Opponent().GetLevel(), *FindOpponentLevel("max"));
        PressWith(kSetColorAction, "black");
        PressWith(kSetModeAction, "computer");
        EXPECT_EQ(Opponent().GetPlays(), Optional<PieceColor>(PieceColor::White));

        for (const PanelSegment& segment : ExpectedPanelSegments()) {
            const PoolString action = ClickerOf(PanelButton(segment.name.c_str()))->action;
            const bool known = action == PoolString::Intern(kSetModeAction) || action == PoolString::Intern(kSetColorAction) || action == PoolString::Intern(kSetEngineAction) ||
                               action == PoolString::Intern(kSetLevelAction);
            EXPECT_TRUE(known) << segment.name.c_str() << " emits " << action.CStr() << ", which ChessOpponentPanelComponent does not answer";
        }
        EXPECT_EQ(ClickerOf(PanelButton("OpponentBack"))->action, "ChessOpenMenu"_intern);
        EXPECT_EQ(Panel()->GetRoot().FindByName("OpponentClose"_intern), nullptr) << "Back is the one button: the dim closes all";
        EXPECT_EQ(ClickerOf(*HudElement("Menu"))->action, "ChessOpenMenu"_intern);
    }

    // ---- the opponent panel: broken copies, so that the checks can fail --------------------------------------------------------------
    // Each copy is the text of the shipped panel with one edit (ShippedElementsXml), laid out alone

    TEST_F(ChessUiTest, AWordCutByAFontThatIsTooBigIsCaughtByTheFitCheckAndNotByTheValidator) {
        String xml = ShippedElementsXml("OpponentPanel");
        ASSERT_FALSE(xml.empty());
        ASSERT_NO_FATAL_FAILURE(Replace(xml, R"(name="EngineOwn" flex="1" height="56" fontSize="24")", R"(name="EngineOwn" flex="1" height="56" fontSize="44")"));
        CanvasCopy copy(xml);

        EXPECT_FALSE(HasIssue(ValidateWithWords(copy.layout), UILayoutIssueKind::TextDoesNotFit)) << "an ellipsis label can shrink: the validator does not report it";
        const String problems = PanelProblemsOnMatrix(copy.layout);
        EXPECT_NE(problems.find("EngineOwn cuts 'Own engine'"), String::npos) << problems.c_str();

        // With the clip the validator reports it too
        String clipped = ShippedElementsXml("OpponentPanel");
        ASSERT_NO_FATAL_FAILURE(Replace(clipped, R"(name="EngineOwn" flex="1" height="56" fontSize="24")", R"(name="EngineOwn" flex="1" height="56" fontSize="44")"));
        ASSERT_NO_FATAL_FAILURE(Replace(clipped, R"(text="Own engine")", R"(text="Own engine" overflow="Clip")"));
        CanvasCopy clippedCopy(clipped);
        EXPECT_TRUE(HasIssue(ValidateWithWords(clippedCopy.layout), UILayoutIssueKind::TextDoesNotFit));
    }

    TEST_F(ChessUiTest, ALongerWordInTheNarrowestWindowIsCaught) {
        // A word that fits in the plate of a wide window but not in a quarter of the Level row on the narrowest one
        String xml = ShippedElementsXml("OpponentPanel");
        ASSERT_NO_FATAL_FAILURE(Replace(xml, R"(text="Medium")", R"(text="Intermediate")"));
        CanvasCopy copy(xml);
        LayOutInTheNarrowestWindow(copy.layout);
        const String problems = PanelProblems(copy.layout);
        EXPECT_NE(problems.find("LevelMedium cuts 'Intermediate'"), String::npos) << problems.c_str();
    }

    TEST_F(ChessUiTest, ASegmentUnderTheTouchSizeIsCaught) {
        String xml = ShippedElementsXml("OpponentPanel");
        ASSERT_NO_FATAL_FAILURE(Replace(xml, R"(name="LevelMax" flex="1" height="56")", R"(name="LevelMax" flex="1" height="40")"));
        CanvasCopy copy(xml);
        EXPECT_TRUE(HasIssue(ValidateWithWords(copy.layout), UILayoutIssueKind::TouchTargetTooSmall));
        EXPECT_NE(PanelProblemsOnMatrix(copy.layout).find("LevelMax is 40.0 units high"), String::npos);
    }

    TEST_F(ChessUiTest, ASegmentWithoutAnExplicitHeightHasNoRectAndIsCaughtHere) {
        // The gotcha of the widget: flex shares the width, not the height, so a segment without `height` gets a rect of height 0. It is not drawn, a click cannot find it
        // (`ui_click` answers "hidden") and the validator skips an empty rect, so only PanelProblems() sees it
        String xml = ShippedElementsXml("OpponentPanel");
        ASSERT_NO_FATAL_FAILURE(Replace(xml, R"(name="LevelHard" flex="1" height="56")", R"(name="LevelHard" flex="1")"));
        CanvasCopy copy(xml);
        LayOutInTheNarrowestWindow(copy.layout);
        const UIElement* hard = copy.layout.root->FindByName("LevelHard"_intern);
        ASSERT_NE(hard, nullptr);
        EXPECT_NEAR(hard->rect.h, 0.0f, kPixelTolerance);
        const String problems = PanelProblems(copy.layout);
        EXPECT_NE(problems.find("LevelHard 'Hard' has an empty rect"), String::npos) << problems.c_str();
    }

    TEST_F(ChessUiTest, AnErrorSentenceWiderThanThePlateIsCaught) {
        // A plate 200 units wide cannot hold the longest sentences of the error line; the same copy with the shipped width is clean
        String xml = ShippedElementsXml("OpponentPanel");
        ASSERT_NO_FATAL_FAILURE(Replace(xml, R"(maxWidth="640")", R"(maxWidth="200")"));
        CanvasCopy copy(xml);
        const String problems = PanelProblemsOnMatrix(copy.layout, 100000);
        EXPECT_NE(problems.find("OpponentError cuts"), String::npos) << problems.c_str();

        CanvasCopy shipped(ShippedElementsXml("OpponentPanel"));
        EXPECT_TRUE(PanelProblemsOnMatrix(shipped.layout).empty()) << PanelProblemsOnMatrix(shipped.layout).c_str();
    }

    TEST_F(ChessUiTest, APlateTallerThanTheSafeAreaIsCaught) {
        String xml = ShippedElementsXml("OpponentPanel");
        ASSERT_NO_FATAL_FAILURE(Replace(xml, R"(height="460")", R"(height="1200")"));
        CanvasCopy copy(xml);
        LayOutInTheNarrowestWindow(copy.layout);
        EXPECT_NE(PanelProblems(copy.layout).find("leaves the safe area"), String::npos);
    }

    // ---- the opponent -----------------------------------------------------------------------------------------------------
    // The engines are the doubles of TestEngines.h, given to the controller of the running game by SetEngineFactory: no child process, and a Stockfish on the PATH of
    // this machine is never looked for. The scene is the shipped one, so a click is a click on the board and a frame is the update of every component

    TEST_F(ChessUiTest, WithNobodyAsTheOpponentTheGamePlaysAsTwoPeople) {
        CountingFactory factory({New<InstantEngine>()});
        Opponent().SetEngineFactory(factory.AsFactory());
        _manager->UpdateAll();

        ClickMove("e2", "e4");
        IdleFrames();
        EXPECT_EQ(Model().GetMoves().size(), 1u) << "black is a person: nothing answers";
        ClickMove("e7", "e5");
        IdleFrames();

        EXPECT_EQ(Model().GetMoves().size(), 2u);
        EXPECT_EQ(Model().GetTurn(), PieceColor::White);
        EXPECT_FALSE(Opponent().GetPlays().has_value());
        EXPECT_FALSE(Opponent().IsThinking());
        EXPECT_FALSE(Opponent().GetError().has_value());
        EXPECT_EQ(Opponent().GetRevision(), 0u) << "the start of the game picked nobody and changed nothing";
        EXPECT_EQ(factory.Calls(), 0) << "nothing is looked up until a side is picked";
    }

    TEST_F(ChessUiTest, TheProgramAnswersAPersonsMoveWithinAFewFrames) {
        const auto engine = New<InstantEngine>();
        CountingFactory factory({engine});
        Opponent().SetEngineFactory(factory.AsFactory());
        _manager->UpdateAll();
        ASSERT_TRUE(Opponent().SetSide(PieceColor::Black).has_value());

        IdleFrames();
        EXPECT_EQ(engine->Calls(), 0) << "white is the person's: the program waits";

        ClickMove("e2", "e4");
        ASSERT_TRUE(FramesUntil([&] { return Model().GetMoves().size() == 2; }));
        EXPECT_EQ(Model().GetMoves()[0].ToUci(), "e2e4");
        EXPECT_EQ(Model().GetPieceAt(Model().GetMoves()[1].To()).color, PieceColor::Black) << "the program's move is a black one";
        EXPECT_EQ(Model().GetTurn(), PieceColor::White);
        EXPECT_FALSE(Opponent().IsThinking());

        ClickMove("d2", "d4");
        ASSERT_TRUE(FramesUntil([&] { return Model().GetMoves().size() == 4; }));
        EXPECT_EQ(engine->Calls(), 2) << "one search per move of the person";
        EXPECT_EQ(factory.Calls(), 1);
        EXPECT_FALSE(Opponent().GetError().has_value());
    }

    TEST_F(ChessUiTest, WhileTheProgramThinksAClickOnTheBoardNeverMovesTheGameOrCancelsItsSearch) {
        SKIP_WITHOUT_WORKER_THREADS();
        const auto engine = New<GatedEngine>(true);
        CountingFactory factory({engine});
        Opponent().SetEngineFactory(factory.AsFactory());
        _manager->UpdateAll();
        ASSERT_TRUE(Opponent().SetSide(PieceColor::Black).has_value());
        ClickMove("e2", "e4");
        ASSERT_TRUE(FramesUntil([&] { return engine->Calls() == 1; })) << "the program has the search and waits at its gate";
        ASSERT_TRUE(Opponent().IsThinking());
        const uint64_t version = Model().GetVersion();

        Click("e7");  // a black piece, and it is black's turn: the model would select it for a person
        EXPECT_FALSE(Model().HasSelection());
        Click("e5");
        Click("d2");
        Click("a4");
        IdleFrames();

        EXPECT_FALSE(Model().HasSelection());
        EXPECT_EQ(Model().GetMoves().size(), 1u);
        EXPECT_EQ(Model().GetVersion(), version) << "the clicks did not move the game, so the search is not stale";
        EXPECT_TRUE(Opponent().IsThinking());
        EXPECT_FALSE(engine->WasCancelled());

        engine->Open();
        ASSERT_TRUE(FramesUntil([&] { return Model().GetMoves().size() == 2; })) << "the answer to the search the clicks left alone";
        Click("d2");
        EXPECT_TRUE(Model().HasSelection()) << "white is the person's again: the board takes clicks";
    }

    // ---- the words and the actions of the opponent ------------------------------------------------------------------------
    // The HUD shows `turn` and `status` and the panel the ids of the choice; the layout is another matter, so these tests press the actions the segments emit (or
    // click the segments where they are drawn) and read the variables the HUD publishes. The engines are the doubles of TestEngines.h

    TEST_F(ChessUiTest, TheOpponentWordsStartWithTwoPlayersAtTheMediumLevel) {
        EXPECT_EQ(Var("opponentMode"), "two");
        EXPECT_EQ(Var("opponentLevel"), "medium");
        EXPECT_EQ(Var("turn"), "White to move");
        EXPECT_EQ(Var("status"), "");
    }

    TEST_F(ChessUiTest, TheBoardDoesNotPublishASideVariable) {
        Draw();
        EXPECT_TRUE(CoreManager::GetUIVars().Has("opponentMode"_intern)) << "the HUD publishes its variables: the check below is not about a missing frame";
        EXPECT_FALSE(CoreManager::GetUIVars().Has("side"_intern));
        PressWith("ChessSetLevel", "hard");  // a pick that needs no engine while nobody plays
        Draw();
        EXPECT_FALSE(CoreManager::GetUIVars().Has("side"_intern));
    }

    TEST_F(ChessUiTest, ModeAndColorThroughThePanelPickTheSideTheProgramPlays) {
        // The mode and the color are two choices, so the case goes by their segments
        const auto engine = New<InstantEngine>();
        CountingFactory factory({engine});
        Opponent().SetEngineFactory(factory.AsFactory());
        _manager->UpdateAll();
        ASSERT_TRUE(OpenThePanel());

        ASSERT_TRUE(ClickPanel("ModeComputer").Both());
        EXPECT_EQ(Var("opponentMode"), "computer");
        EXPECT_TRUE(Opponent().PlaysFor(PieceColor::Black));
        ASSERT_TRUE(CloseThePanel());
        ClickMove("e2", "e4");
        ASSERT_TRUE(FramesUntil([&] { return Model().GetMoves().size() == 2; }));
        EXPECT_EQ(Model().GetPieceAt(Model().GetMoves()[1].To()).color, PieceColor::Black) << "the person has White, the program answers with a black piece";

        ASSERT_TRUE(OpenThePanel());
        ASSERT_TRUE(ClickPanel("ColorBlack").Both());
        EXPECT_EQ(Var("opponentColor"), "black");
        EXPECT_TRUE(Opponent().PlaysFor(PieceColor::White));
        ASSERT_TRUE(CloseThePanel());
        ASSERT_TRUE(FramesUntil([&] { return Model().GetMoves().size() == 3; })) << "it is White's turn and White is the program's now";
        EXPECT_EQ(Model().GetPieceAt(Model().GetMoves()[2].To()).color, PieceColor::White);

        ASSERT_TRUE(OpenThePanel());
        ASSERT_TRUE(ClickPanel("ModeTwo").Both());
        EXPECT_EQ(Var("opponentMode"), "two");
        EXPECT_FALSE(Opponent().PlaysFor(PieceColor::White));
        EXPECT_FALSE(Opponent().PlaysFor(PieceColor::Black));
        ASSERT_TRUE(CloseThePanel());
        const MoveList legal = MoveGenerator::GenerateLegal(Model().GetPosition());
        ASSERT_FALSE(legal.empty());
        ASSERT_TRUE(Model().TryMove(legal.front()).has_value()) << "Black's move, made by a person";
        IdleFrames();
        EXPECT_EQ(Model().GetMoves().size(), 4u) << "nobody answers it";
        EXPECT_EQ(engine->Calls(), 2) << "one search for each of the two sides, none for two people";

        ASSERT_TRUE(OpenThePanel());
        ASSERT_TRUE(ClickPanel("ModeComputer").Both());
        EXPECT_EQ(Var("opponentColor"), "black") << "and round again: the color the person had is kept while two people played";
        ASSERT_TRUE(ClickPanel("ColorWhite").Both());
        EXPECT_EQ(Var("opponentColor"), "white");
        EXPECT_EQ(factory.Calls(), 4) << "an engine for each pick that makes the program play (Computer, Black, Computer, White), none for two people";
    }

    TEST_F(ChessUiTest, TheLevelActionPicksEveryLevelAndTheFactoryIsAskedOnlyWhenTheProgramPlays) {
        // The segments of LevelGroup emit ChessSetLevel with the id of their level; the test presses the action the same way
        const auto levels = GetOpponentLevels();
        CountingFactory factory({New<InstantEngine>()});
        Opponent().SetEngineFactory(factory.AsFactory());
        _manager->UpdateAll();

        for (size_t index = 0; index < levels.size(); ++index) {
            const String id(levels[index].id.data(), levels[index].id.size());
            PressWith(kSetLevelAction, id.c_str());
            EXPECT_EQ(Opponent().GetLevel(), index) << id.c_str();
            EXPECT_EQ(Var("opponentLevel"), id) << id.c_str();
        }
        EXPECT_EQ(factory.Calls(), 0) << "with nobody as the opponent a level is only a choice: no engine is asked for";

        PressWith(kSetModeAction, "computer");
        PressWith(kSetLevelAction, "easy");
        EXPECT_EQ(Var("opponentLevel"), "easy");
        ASSERT_EQ(factory.Calls(), 2) << "one engine for the pick, another for the level";
        EXPECT_EQ(factory.Levels()[0], levels[levels.size() - 1].id) << "the pick used the level that was picked last";
        EXPECT_EQ(factory.Levels()[1], "easy");
    }

    TEST_F(ChessUiTest, TheEngineSegmentsPickBothEnginesAndTheDefaultIsStockfish) {
        SKIP_WITHOUT_STOCKFISH_SEGMENT();
        CountingFactory factory({New<InstantEngine>()});
        Opponent().SetEngineFactory(factory.AsFactory());
        _manager->UpdateAll();
        StartFromTheStockfishName();
        ASSERT_TRUE(OpenThePanel());
        EXPECT_EQ(Var("opponentEngine"), "stockfish");
        EXPECT_TRUE(PanelButton("EngineStockfish").selected) << "the default (where Stockfish is available)";
        EXPECT_FALSE(PanelButton("EngineOwn").selected);

        ASSERT_TRUE(ClickPanel("EngineOwn").Both());
        EXPECT_EQ(Var("opponentEngine"), "own");
        EXPECT_EQ(Opponent().GetEngine(), OpponentEngine::Own);
        EXPECT_TRUE(PanelButton("EngineOwn").selected);
        EXPECT_FALSE(PanelButton("EngineStockfish").selected);
        ASSERT_TRUE(ClickPanel("EngineStockfish").Both());
        EXPECT_EQ(Var("opponentEngine"), "stockfish") << "and back";
        EXPECT_TRUE(PanelButton("EngineStockfish").selected);
        EXPECT_EQ(factory.Calls(), 0) << "with nobody as the opponent an engine is only a choice: none is asked for";

        ASSERT_TRUE(ClickPanel("ModeComputer").Both());
        ASSERT_EQ(factory.Calls(), 1);
        EXPECT_EQ(factory.Engines()[0], "stockfish");
        ASSERT_TRUE(ClickPanel("EngineOwn").Both());
        EXPECT_EQ(Var("opponentEngine"), "own");
        ASSERT_EQ(factory.Calls(), 2) << "the program plays: a new engine for the new choice";
        EXPECT_EQ(factory.Engines()[1], "own");
        EXPECT_EQ(Opponent().GetPlays(), Optional<PieceColor>(PieceColor::Black)) << "the side stays";
    }

    TEST_F(ChessUiTest, WhileTheProgramThinksTheTurnSaysSoAndTheAnswerBringsTheUsualWordsBack) {
        SKIP_WITHOUT_WORKER_THREADS();
        const auto engine = New<GatedEngine>(true);
        CountingFactory factory({engine});
        Opponent().SetEngineFactory(factory.AsFactory());
        _manager->UpdateAll();
        PressWith(kSetModeAction, "computer");
        ASSERT_TRUE(Opponent().PlaysFor(PieceColor::Black));

        ClickMove("e2", "e4");
        EXPECT_EQ(Var("turn"), "Black to move") << "the person has moved, the search has not started yet";
        ASSERT_TRUE(FramesUntil([&] { return engine->Calls() == 1; }));
        ASSERT_TRUE(Opponent().IsThinking());
        const uint64_t version = Model().GetVersion();

        EXPECT_EQ(Var("turn"), "Computer is thinking") << "the search began without a move of the game: the words follow the search, not only the version";
        EXPECT_EQ(Model().GetVersion(), version);

        engine->Open();
        ASSERT_TRUE(FramesUntil([&] { return Model().GetMoves().size() == 2; }));
        EXPECT_FALSE(Opponent().IsThinking());
        EXPECT_EQ(Var("turn"), "White to move");
    }

    TEST_F(ChessUiTest, ThePickOfAnEngineThatIsNotFoundIsSaidInTheStatusAndTheNextMoveTakesItAway) {
        CountingFactory factory(EngineError{EngineErrorKind::LaunchFailed, "no Stockfish in the test"});
        Opponent().SetEngineFactory(factory.AsFactory());
        _manager->UpdateAll();

        PressWith(kSetModeAction, "computer");

        EXPECT_EQ(Var("status"), "Engine not found");
        EXPECT_EQ(Var("opponentMode"), "two") << "a pick that found nothing leaves two people on the screen";
        EXPECT_EQ(Var("turn"), "White to move");

        ClickMove("e2", "e4");
        _manager->UpdateAll();

        EXPECT_EQ(Var("status"), "") << "the person has played on: the sentence has been seen";
        EXPECT_EQ(Var("opponentMode"), "two");
        EXPECT_EQ(factory.Calls(), 1);
    }

    TEST_F(ChessUiTest, TheSentenceOfASearchErrorIsInTheStatusUntilSomeoneMovesTheGame) {
        const auto engine = New<FailingEngine>(EngineError{EngineErrorKind::LaunchFailed, "the program cannot be started"});
        CountingFactory factory({engine});
        ASSERT_NO_FATAL_FAILURE(ProvokeAnEngineError(factory));

        EXPECT_EQ(Var("status"), "Engine not found");
        EXPECT_EQ(Var("opponentMode"), "computer") << "the program is still Black, only the game waits";
        IdleFrames();
        EXPECT_EQ(Var("status"), "Engine not found") << "frames do not take it away";

        ASSERT_TRUE(Play(Model(), "e7e5")) << "a move of someone else, the way the control API plays";
        _manager->UpdateAll();

        EXPECT_EQ(Var("status"), "");
        EXPECT_EQ(Var("turn"), "White to move");
    }

    TEST_F(ChessUiTest, TheSentenceOfASearchErrorWinsOverCheckAndTheResetButtonTakesItAway) {
        const auto engine = New<FailingEngine>(EngineError{EngineErrorKind::LaunchFailed, "the program cannot be started"});
        CountingFactory factory({engine});
        Opponent().SetEngineFactory(factory.AsFactory());
        _manager->UpdateAll();
        ASSERT_TRUE(Opponent().SetSide(PieceColor::Black).has_value());
        Model().SetPosition(PositionFromFen("4k3/8/8/8/8/8/4R3/4K3 b - - 0 1"));  // Black, the program's side, is in check
        ASSERT_TRUE(Model().IsInCheck());
        ASSERT_TRUE(FramesUntil([&] { return Opponent().GetError().has_value(); }));

        EXPECT_EQ(Var("status"), "Engine not found") << "the game is stalled by the error: it is what the person has to read, not 'Check'";
        EXPECT_TRUE(Model().IsInCheck());

        Press("ChessReset");
        _manager->UpdateAll();

        EXPECT_EQ(Var("status"), "");
        EXPECT_EQ(Var("turn"), "White to move");
    }

    TEST_F(ChessUiTest, TheEngineGroupTakesClicksWhetherOrNotTheProgramPlays) {
        // No group of the panel is greyed, and the EngineGroup works in every state
        CountingFactory working({New<InstantEngine>()});
        Opponent().SetEngineFactory(working.AsFactory());
        _manager->UpdateAll();
        ASSERT_TRUE(OpenThePanel());

        const auto engineGroupWorks = [&](const char* state) {
            SCOPED_TRACE(state);
            Draw();
            EXPECT_FALSE(PanelControl<UISegmented>("EngineGroup").disabled);
            EXPECT_FALSE(PanelButton("EngineOwn").rect.IsEmpty());
            ASSERT_TRUE(ClickPanel("EngineOwn").Both());
            EXPECT_EQ(Opponent().GetEngine(), OpponentEngine::Own);
            if constexpr (kStockfishAvailable) {
                ASSERT_TRUE(ClickPanel("EngineStockfish").Both());
                EXPECT_EQ(Opponent().GetEngine(), OpponentEngine::Stockfish);
            }
        };
        engineGroupWorks("nobody plays: it is how the person gets to the own engine");
        ASSERT_TRUE(ClickPanel("ModeComputer").Both());
        engineGroupWorks("the program plays Black");
        ASSERT_TRUE(ClickPanel("ColorBlack").Both());
        engineGroupWorks("the program plays White");
        ASSERT_TRUE(ClickPanel("ModeTwo").Both());
        engineGroupWorks("two people again");

        CountingFactory failing(EngineError{EngineErrorKind::LaunchFailed, "no Stockfish in the test"});
        Opponent().SetEngineFactory(failing.AsFactory());
        ASSERT_TRUE(ClickPanel("ModeComputer").Both());
        EXPECT_EQ(Var("status"), "Engine not found");
        engineGroupWorks("after a refused pick (nobody was playing, and nothing changed): the group is what the person needs then");
    }

    // ---- the buttons of the opponent, clicked where they are drawn ---------------------------------------------------------------
    // The fill of each button has a color of its own in SceneConfig.xml, which is how a test finds where the screen shows it (the way ClickingTheResetButtonOnTheScreen... does)

    TEST_F(ChessUiTest, ClickingTheModeAndColorSegmentsOnTheScreenPicksTheSideAndAPressAloneDoesNothing) {
        CountingFactory factory({New<InstantEngine>()});
        Opponent().SetEngineFactory(factory.AsFactory());
        _manager->UpdateAll();
        ASSERT_TRUE(OpenThePanel());
        Draw();
        ASSERT_EQ(ShownText(Label("Turn")), "White to move");

        const auto at = ScreenCenter(PanelElement("ModeComputer"));
        EXPECT_TRUE(CoreManager::GetUIInput().DispatchMouseButtonDown(Button(at.first, at.second, true))) << "the segment takes the click";
        EXPECT_FALSE(Opponent().GetPlays().has_value()) << "a press alone does nothing";
        EXPECT_TRUE(CoreManager::GetUIInput().DispatchMouseButtonUp(Button(at.first, at.second, false)));
        EXPECT_EQ(Opponent().GetPlays(), Optional<PieceColor>(PieceColor::Black));

        EXPECT_TRUE(ClickPanel("ColorBlack").Both());
        EXPECT_EQ(Opponent().GetPlays(), Optional<PieceColor>(PieceColor::White));

        EXPECT_TRUE(ClickPanel("ModeTwo").Both());
        EXPECT_FALSE(Opponent().GetPlays().has_value());
        EXPECT_FALSE(PanelButton("ModeComputer").rect.IsEmpty()) << "the way back to a pick is still there";
    }

    TEST_F(ChessUiTest, ClickingAnEngineSegmentOnTheScreenPicksTheEngineAndAPressAloneDoesNothing) {
        SKIP_WITHOUT_STOCKFISH_SEGMENT();
        CountingFactory factory({New<InstantEngine>()});
        Opponent().SetEngineFactory(factory.AsFactory());
        _manager->UpdateAll();
        StartFromTheStockfishName();
        ASSERT_TRUE(OpenThePanel());
        Draw();
        ASSERT_EQ(ShownText(PanelElement("EngineStockfish")), "Stockfish");
        ASSERT_EQ(ShownText(PanelElement("EngineOwn")), "Own engine");

        const auto at = ScreenCenter(PanelElement("EngineOwn"));
        EXPECT_TRUE(CoreManager::GetUIInput().DispatchMouseButtonDown(Button(at.first, at.second, true))) << "the segment takes the click";
        EXPECT_EQ(Opponent().GetEngine(), OpponentEngine::Stockfish) << "a press alone does nothing";
        EXPECT_TRUE(CoreManager::GetUIInput().DispatchMouseButtonUp(Button(at.first, at.second, false)));
        EXPECT_EQ(Opponent().GetEngine(), OpponentEngine::Own);
        EXPECT_EQ(Var("opponentEngine"), "own");
        EXPECT_TRUE(PanelButton("EngineOwn").selected) << "the selected segment follows";

        ASSERT_TRUE(ClickPanel("ModeComputer").Both());  // the program plays Black
        ASSERT_EQ(factory.Calls(), 1);
        EXPECT_EQ(factory.Engines()[0], "own");
        EXPECT_TRUE(ClickPanel("EngineStockfish").Both()) << "also while the program plays";
        EXPECT_EQ(Opponent().GetEngine(), OpponentEngine::Stockfish);
        EXPECT_EQ(Var("opponentEngine"), "stockfish");
        EXPECT_TRUE(PanelButton("EngineStockfish").selected);
        ASSERT_EQ(factory.Calls(), 2);
        EXPECT_EQ(factory.Engines()[1], "stockfish");
        EXPECT_EQ(Opponent().GetPlays(), Optional<PieceColor>(PieceColor::Black)) << "the side stays";
    }

    TEST_F(ChessUiTest, APressOnASegmentDoesNotChangeTheBoardSelection) {
        CountingFactory factory({New<InstantEngine>()});
        Opponent().SetEngineFactory(factory.AsFactory());
        _manager->UpdateAll();
        Click("e2");
        ASSERT_TRUE(Model().HasSelection());

        ASSERT_TRUE(OpenThePanel());
        EXPECT_TRUE(ClickPanel("EngineOwn").Both());
        EXPECT_TRUE(ClickPanel("ModeComputer").Both());  // the program takes Black, which is not the side to move: the half-made move of the person stays

        EXPECT_EQ(Opponent().GetEngine(), OpponentEngine::Own);
        EXPECT_TRUE(Model().HasSelection());
        EXPECT_TRUE(Model().GetSelected() == Sq("e2"));
        ASSERT_TRUE(CloseThePanel());
        EXPECT_TRUE(Model().HasSelection());
    }

    TEST_F(ChessUiTest, TheOwnEngineOfTheDefaultFactoryPlaysThroughThePanel) {
        // No factory is set: the controller makes the real OwnEngine (no process, no settings) at the easy level, so the test needs no Stockfish and no fake program
        const Optional<size_t> easy = FindOpponentLevel("easy");
        ASSERT_TRUE(easy.has_value());
        ASSERT_TRUE(OpenThePanel());
        ASSERT_TRUE(ClickPanel("EngineOwn").Both());
        ASSERT_EQ(Opponent().GetEngine(), OpponentEngine::Own);
        ASSERT_TRUE(ClickPanel("ModeComputer").Both());
        EXPECT_EQ(Var("opponentMode"), "computer");
        ASSERT_TRUE(ClickPanel("LevelEasy").Both());
        ASSERT_EQ(Opponent().GetLevel(), *easy);
        EXPECT_EQ(Var("opponentLevel"), "easy");
        EXPECT_FALSE(Opponent().GetError().has_value());
        ASSERT_TRUE(CloseThePanel());

        ClickMove("e2", "e4");
        ASSERT_TRUE(FramesUntil([&] { return Model().GetMoves().size() == 2; })) << "the own engine searched and its move was played";
        EXPECT_EQ(Model().GetMoves()[0].ToUci(), "e2e4");
        EXPECT_EQ(Model().GetPieceAt(Model().GetMoves()[1].To()).color, PieceColor::Black) << "a legal reply for Black (TryMove accepts nothing else)";
        ASSERT_TRUE(FramesUntil([&] { return !Opponent().IsThinking(); }));
        EXPECT_EQ(Model().GetTurn(), PieceColor::White);
        EXPECT_FALSE(Opponent().GetError().has_value());
        EXPECT_EQ(Var("turn"), "White to move");
    }

    TEST_F(ChessUiTest, TheResetButtonKeepsTheEngine) {
        CountingFactory factory({New<InstantEngine>()});
        Opponent().SetEngineFactory(factory.AsFactory());
        _manager->UpdateAll();
        ASSERT_TRUE(OpenThePanel());
        ASSERT_TRUE(ClickPanel("EngineOwn").Both());
        ASSERT_TRUE(ClickPanel("ModeComputer").Both());
        ASSERT_TRUE(CloseThePanel());
        ClickMove("e2", "e4");
        ASSERT_TRUE(FramesUntil([&] { return Model().GetMoves().size() == 2; }));

        EXPECT_TRUE(ClickButton(kResetColor));

        EXPECT_EQ(Model().GetPosition().ToFen(), String(Position::kStartFen));
        EXPECT_EQ(Opponent().GetEngine(), OpponentEngine::Own) << "a new game keeps the engine";
        EXPECT_EQ(Var("opponentEngine"), "own");
        EXPECT_EQ(Opponent().GetPlays(), Optional<PieceColor>(PieceColor::Black));
    }

    TEST_F(ChessUiTest, TheResetButtonStillResetsWithTheMenuButtonAround) {
        // Reset keeps the opponent chosen in the panel; the Menu button beside it does not change that
        CountingFactory factory({New<InstantEngine>()});
        Opponent().SetEngineFactory(factory.AsFactory());
        _manager->UpdateAll();
        ASSERT_TRUE(OpenThePanel());
        ASSERT_TRUE(ClickPanel("ModeComputer").Both());
        ASSERT_TRUE(CloseThePanel());
        ClickMove("e2", "e4");
        ASSERT_TRUE(FramesUntil([&] { return Model().GetMoves().size() == 2; }));

        EXPECT_TRUE(ClickButton(kResetColor));

        EXPECT_EQ(Model().GetPosition().ToFen(), String(Position::kStartFen));
        EXPECT_EQ(Opponent().GetPlays(), Optional<PieceColor>(PieceColor::Black)) << "a new game keeps the opponent";
        Draw();
        EXPECT_EQ(ShownText(*HudElement("Menu")), "Menu");
    }

    TEST_F(ChessUiTest, ThePressOfAHudButtonDoesNotReachTheBoardAndTheBoardStillGetsItsOwnClicks) {
        CountingFactory factory({New<InstantEngine>()});
        Opponent().SetEngineFactory(factory.AsFactory());
        _manager->UpdateAll();
        ASSERT_TRUE(OpenThePanel());
        ASSERT_TRUE(ClickPanel("ModeComputer").Both());
        ASSERT_TRUE(CloseThePanel());
        Draw();

        // A click on the board is not taken by the HUD (it has no control there) and the board selects the piece
        const auto onTheBoard = SquareCenter("e2");
        EXPECT_FALSE(CoreManager::GetUIInput().DispatchMouseButtonDown(Button(onTheBoard.first, onTheBoard.second, true)));
        EXPECT_TRUE(Model().HasSelection()) << "the person has White: the board took the click";
        EXPECT_TRUE(Model().GetSelected() == Sq("e2"));

        // And a press on a button of the HUD is the HUD's: the selection is not changed by it
        EXPECT_TRUE(ClickButton(kMenuColor));
        EXPECT_TRUE(MenuList()->_visible);
        EXPECT_TRUE(Model().HasSelection());
        EXPECT_TRUE(Model().GetSelected() == Sq("e2"));
    }

    // ---- the choice of the opponent panel: the actions with a value and the ids --------------------------------------------
    // The segments of the panel emit ChessSetMode / ChessSetColor / ChessSetEngine / ChessSetLevel with the id as `value`; ChessOpponentPanelComponent answers through OpponentChoice,
    // ChessHudComponent publishes the ids. These tests press the actions the segments emit and read the variables

    TEST_F(ChessUiTest, TheIdsOfThePanelStartAsTwoWhiteTheDefaultEngineMediumAndNoError) {
        EXPECT_EQ(Var("opponentMode"), "two");
        EXPECT_EQ(Var("opponentColor"), "white");
        EXPECT_EQ(Var("opponentEngine"), kStockfishAvailable ? "stockfish" : "own");
        EXPECT_EQ(Var("opponentLevel"), "medium");
        EXPECT_EQ(Var("opponentError"), "");
    }

    TEST_F(ChessUiTest, EachActionOfThePanelPicksWhatTheControlApiWouldRead) {
        CountingFactory factory({New<InstantEngine>()});
        Opponent().SetEngineFactory(factory.AsFactory());
        _manager->UpdateAll();
        const size_t hard = *FindOpponentLevel("hard");

        // Nobody plays: each of the three is only a choice, no engine is asked for
        PressWith("ChessSetEngine", "own");
        EXPECT_EQ(Opponent().GetEngine(), OpponentEngine::Own);
        EXPECT_EQ(Var("opponentEngine"), "own");
        PressWith("ChessSetLevel", "hard");
        EXPECT_EQ(Opponent().GetLevel(), hard);
        EXPECT_EQ(Var("opponentLevel"), "hard");
        PressWith("ChessSetColor", "black");
        EXPECT_FALSE(Opponent().GetPlays().has_value()) << "the color of the person is remembered, nobody plays";
        EXPECT_EQ(Var("opponentColor"), "black");
        EXPECT_EQ(Var("opponentMode"), "two");
        EXPECT_EQ(factory.Calls(), 0);

        // Computer: the program plays the color the person does not, with the engine and the level that were picked, one engine made
        PressWith("ChessSetMode", "computer");
        EXPECT_EQ(Opponent().GetPlays(), Optional<PieceColor>(PieceColor::White)) << "the person has Black";
        EXPECT_EQ(Var("opponentMode"), "computer");
        EXPECT_EQ(Var("opponentColor"), "black");
        ASSERT_EQ(factory.Calls(), 1);
        EXPECT_EQ(factory.Engines()[0], "own");
        EXPECT_EQ(factory.Levels()[0], "hard");

        // While the program plays each pick remakes the engine once
        PressWith("ChessSetEngine", "stockfish");
        ASSERT_EQ(factory.Calls(), 2);
        EXPECT_EQ(factory.Engines()[1], "stockfish");
        EXPECT_EQ(Var("opponentEngine"), "stockfish");
        PressWith("ChessSetLevel", "easy");
        ASSERT_EQ(factory.Calls(), 3);
        EXPECT_EQ(factory.Levels()[2], "easy");
        EXPECT_EQ(Var("opponentLevel"), "easy");
        PressWith("ChessSetColor", "white");
        ASSERT_EQ(factory.Calls(), 4);
        EXPECT_EQ(Opponent().GetPlays(), Optional<PieceColor>(PieceColor::Black));
        EXPECT_EQ(Var("opponentColor"), "white");

        PressWith("ChessSetMode", "two");
        EXPECT_FALSE(Opponent().GetPlays().has_value());
        EXPECT_EQ(Var("opponentMode"), "two");
        EXPECT_EQ(Var("opponentColor"), "white");
        EXPECT_EQ(factory.Calls(), 4) << "nobody plays: nothing is asked";
    }

    TEST_F(ChessUiTest, ComputerAndThenBlackMakesTheProgramOpenAsWhite) {
        CountingFactory factory({New<InstantEngine>()});
        Opponent().SetEngineFactory(factory.AsFactory());
        _manager->UpdateAll();

        PressWith("ChessSetMode", "computer");
        EXPECT_TRUE(Opponent().PlaysFor(PieceColor::Black));
        PressWith("ChessSetColor", "black");
        EXPECT_TRUE(Opponent().PlaysFor(PieceColor::White));
        EXPECT_FALSE(Opponent().PlaysFor(PieceColor::Black));

        ASSERT_TRUE(FramesUntil([&] { return Model().GetMoves().size() == 1; })) << "it is White's turn and White is the program's";
        EXPECT_EQ(Model().GetPieceAt(Model().GetMoves()[0].To()).color, PieceColor::White);
        EXPECT_EQ(Var("opponentMode"), "computer");
        EXPECT_EQ(Var("opponentColor"), "black");
    }

    TEST_F(ChessUiTest, ThePersonsColorSurvivesTwoPlayersAndComesBack) {
        CountingFactory factory({New<InstantEngine>()});
        Opponent().SetEngineFactory(factory.AsFactory());
        _manager->UpdateAll();

        PressWith("ChessSetColor", "black");
        PressWith("ChessSetMode", "computer");
        PressWith("ChessSetMode", "two");
        EXPECT_EQ(Var("opponentMode"), "two");
        EXPECT_EQ(Var("opponentColor"), "black") << "the program is gone and the person still has the color";
        PressWith("ChessSetMode", "computer");
        EXPECT_EQ(Opponent().GetPlays(), Optional<PieceColor>(PieceColor::White));
        EXPECT_EQ(Var("opponentColor"), "black");
    }

    TEST_F(ChessUiTest, TheColorPickedWhileTwoPlayIsPublishedWithNoRevisionOfTheOpponent) {
        // The controller knows nothing of the color then, so its revision does not move; the board still has to say the new word
        EXPECT_EQ(Var("opponentColor"), "white");
        const uint64_t revision = Opponent().GetRevision();

        PressWith("ChessSetColor", "black");

        EXPECT_EQ(Opponent().GetRevision(), revision);
        EXPECT_EQ(Var("opponentColor"), "black");
        PressWith("ChessSetColor", "white");
        EXPECT_EQ(Var("opponentColor"), "white");
    }

    TEST_F(ChessUiTest, AnActionOfThePanelWithABadValueChangesNothing) {
        CountingFactory factory({New<InstantEngine>()});
        Opponent().SetEngineFactory(factory.AsFactory());
        _manager->UpdateAll();
        StartFromTheStockfishName();
        PressWith("ChessSetMode", "computer");
        ASSERT_EQ(factory.Calls(), 1);
        const uint64_t revision = Opponent().GetRevision();
        const uint64_t version = Model().GetVersion();

        for (const char* action : {"ChessSetMode", "ChessSetColor", "ChessSetEngine", "ChessSetLevel"}) {
            for (const char* value : {"", "White", "Computer", "OWN", "own ", "bl", "nonsense"}) {
                PressWith(action, value);
            }
            Press(action);  // a plain button press: no value at all
        }

        EXPECT_EQ(factory.Calls(), 1) << "no engine is asked for an id that is in no table";
        EXPECT_EQ(Opponent().GetRevision(), revision);
        EXPECT_EQ(Model().GetVersion(), version);
        EXPECT_EQ(Opponent().GetPlays(), Optional<PieceColor>(PieceColor::Black));
        EXPECT_EQ(Var("opponentMode"), "computer");
        EXPECT_EQ(Var("opponentColor"), "white");
        EXPECT_EQ(Var("opponentEngine"), "stockfish");
        EXPECT_EQ(Var("opponentLevel"), "medium");
        EXPECT_EQ(Var("opponentError"), "");
    }

    TEST_F(ChessUiTest, WhenNoStockfishIsFoundTheEngineRowLeadsToTheOwnEngineAndComputerPlays) {
        // The user's path through the panel's actions: Stockfish is not installed, Computer says so and leaves two people, Own engine and Computer again let the program play.
        // The factory fails for Stockfish and nothing else, so the program cannot have fallen back by itself
        Vector<String> asked;
        Opponent().SetEngineFactory([&asked](OpponentEngine engine, const OpponentLevel&) -> MadeEngine {
            const StringView id = OpponentEngineId(engine);
            asked.emplace_back(id.data(), id.size());
            if (engine == OpponentEngine::Stockfish) {
                return std::unexpected(EngineError{EngineErrorKind::LaunchFailed, "no Stockfish in the test"});
            }
            return New<InstantEngine>();
        });
        _manager->UpdateAll();
        StartFromTheStockfishName();

        PressWith("ChessSetMode", "computer");

        EXPECT_EQ(Var("opponentMode"), "two") << "the screen shows the truth: nobody plays";
        EXPECT_EQ(Var("opponentError"), "Engine not found");
        EXPECT_EQ(Var("status"), "Engine not found");
        EXPECT_EQ(Var("opponentEngine"), "stockfish") << "no silent switch";
        ASSERT_EQ(asked.size(), 1u);

        PressWith("ChessSetEngine", "own");

        EXPECT_EQ(Var("opponentEngine"), "own");
        EXPECT_EQ(Var("opponentError"), "") << "a new pick: the person has seen the sentence";
        EXPECT_EQ(Var("status"), "");
        EXPECT_EQ(asked.size(), 1u) << "with nobody playing the choice only changes";

        PressWith("ChessSetMode", "computer");

        EXPECT_EQ(Var("opponentMode"), "computer");
        EXPECT_EQ(Var("opponentError"), "");
        ASSERT_EQ(asked.size(), 2u);
        EXPECT_EQ(asked[1], "own");
        ClickMove("e2", "e4");
        ASSERT_TRUE(FramesUntil([&] { return Model().GetMoves().size() == 2; })) << "the program answers the person's move";
        EXPECT_EQ(Model().GetPieceAt(Model().GetMoves()[1].To()).color, PieceColor::Black);
        EXPECT_FALSE(Opponent().GetError().has_value());
    }

    TEST_F(ChessUiTest, AFailedSearchIsInTheErrorVariableToo) {
        const auto engine = New<FailingEngine>(EngineError{EngineErrorKind::LaunchFailed, "the program cannot be started"});
        CountingFactory factory({engine});
        ASSERT_NO_FATAL_FAILURE(ProvokeAnEngineError(factory));

        EXPECT_EQ(Var("opponentError"), OpponentText::Error(*Opponent().GetError()));
        EXPECT_EQ(Var("opponentMode"), "computer") << "the pick succeeded, only the first search failed";

        Press("ChessReset");
        _manager->UpdateAll();

        EXPECT_EQ(Var("opponentError"), "") << "the version moved: the error rule clears it";
    }

    TEST_F(ChessUiTest, TheIdsAgreeWithWhatTheControlApiReportsAfterASetState) {
        CountingFactory factory({New<InstantEngine>()});
        Opponent().SetEngineFactory(factory.AsFactory());
        _manager->UpdateAll();
        const auto agree = [&](const char* mode, const char* personColor, const char* plays) {
            const Json opponent = ControlOpponent();
            const auto reported = GetJsonString(opponent, "plays");
            if (plays == nullptr) {
                EXPECT_FALSE(reported.has_value());
            } else {
                ASSERT_TRUE(reported.has_value());
                EXPECT_EQ(String(reported->c_str(), reported->size()), plays);
            }
            EXPECT_EQ(Var("opponentMode"), mode);
            EXPECT_EQ(Var("opponentColor"), personColor) << "the person has the opposite of what the program plays";
            const auto engine = GetJsonString(opponent, "engine");
            const auto level = GetJsonString(opponent, "level");
            ASSERT_TRUE(engine.has_value());
            ASSERT_TRUE(level.has_value());
            EXPECT_EQ(Var("opponentEngine"), *engine);
            EXPECT_EQ(Var("opponentLevel"), *level);
        };

        agree("two", "white", nullptr);

        ControlPlays("black");
        _manager->UpdateAll();
        agree("computer", "white", "black");

        ControlPlays("white");
        _manager->UpdateAll();
        agree("computer", "black", "white");

        ControlPlays(nullptr);
        _manager->UpdateAll();
        agree("two", "black", nullptr);  // the color the person had stays

        // A pick of the panel is read back by the API in the program's color
        PressWith("ChessSetMode", "computer");
        agree("computer", "black", "white");
        PressWith("ChessSetColor", "white");
        agree("computer", "white", "black");
    }

    // ---- the opponent panel, clicked where it is drawn ---------------------------------------------------------------------------
    // The segments are found where the layout put them (their rect, mapped to output pixels) and clicked through CoreManager::GetUIInput(), never through Press: what the
    // screen takes, what reaches the board and what the groups show are the point. The engines are the doubles of TestEngines.h

    TEST_F(ChessUiTest, ThePanelIsHiddenAtTheStartDrawsNothingAndTakesNoClick) {
        auto panel = Panel();
        ASSERT_TRUE(panel);
        EXPECT_FALSE(panel->_visible);
        EXPECT_TRUE(panel->_modal);
        EXPECT_FALSE(Hud()->_modal);
        Draw();
        EXPECT_TRUE(RectCalls(kPlateColor).empty()) << "hidden: not even the plate is drawn";

        // Where a segment will be, the panel takes nothing and the board under it gets the click. Show it once to learn the place, then hide it again
        ASSERT_TRUE(OpenThePanel());
        Draw();
        const auto place = ScreenCenter(PanelButton("LevelMedium"));
        ASSERT_TRUE(CloseThePanel());
        Draw();
        EXPECT_TRUE(RectCalls(kPlateColor).empty());

        ActionLog log;
        const Taken taken = ClickAtPixel(place.first, place.second);
        EXPECT_FALSE(taken.pressed);
        EXPECT_FALSE(taken.released);
        EXPECT_EQ(log.Total(), 0u);
        EXPECT_EQ(Opponent().GetLevel(), kDefaultOpponentLevel);
        const auto e2 = SquareCenter("e2");
        EXPECT_FALSE(CoreManager::GetUIInput().DispatchMouseButtonDown(Button(e2.first, e2.second, true)));
        EXPECT_TRUE(Model().HasSelection()) << "the board under the hidden panel gets its clicks";
    }

    TEST_F(ChessUiTest, TheMenuButtonOpensTheListAndTheListWalksToTheOpponentPanelAndClosesEverything) {
        ActionLog log;
        Draw();
        EXPECT_EQ(ShownText(*HudElement("Menu")), "Menu");
        EXPECT_FALSE(MenuList()->_visible);
        ASSERT_TRUE(ClickButton(kMenuColor));
        EXPECT_EQ(log.LastAction(), "ChessOpenMenu"_intern);
        ASSERT_TRUE(MenuList()->_visible);
        EXPECT_FALSE(Panel()->_visible) << "the list shows no section";
        Draw();
        EXPECT_FALSE(RectCalls(kPlateColor).empty()) << "shown: the plate is drawn";

        // The Menu button is under the dim now: the dim takes the press first, and it is the dim's (it closes the list), not the Menu button's
        const size_t actions = log.Total();
        const auto button = ScreenCenter(*HudElement("Menu"));
        const Taken second = ClickAtPixel(button.first, button.second);
        EXPECT_TRUE(second.Both()) << "the modal canvas takes it";
        EXPECT_EQ(log.Total(), actions + 1);
        EXPECT_EQ(log.LastAction(), "ChessCloseMenu"_intern);
        EXPECT_FALSE(MenuList()->_visible);
        ASSERT_TRUE(ClickButton(kMenuColor));
        ASSERT_TRUE(MenuList()->_visible);

        // The section replaces the list, Back brings the list back, the dim hides both
        ASSERT_TRUE(ClickMenuList("MenuSectionOpponent").Both());
        EXPECT_EQ(log.LastAction(), "ChessOpenSection"_intern);
        EXPECT_TRUE(Panel()->_visible);
        EXPECT_FALSE(MenuList()->_visible);
        ASSERT_TRUE(ClickPanel("OpponentBack").Both());
        EXPECT_EQ(log.LastAction(), "ChessOpenMenu"_intern);
        EXPECT_TRUE(MenuList()->_visible);
        EXPECT_FALSE(Panel()->_visible);
        ASSERT_TRUE(ClickMenuList("MenuSectionOpponent").Both());
        ASSERT_TRUE(CloseThePanel());
        EXPECT_EQ(log.LastAction(), "ChessCloseMenu"_intern);
        Draw();
        EXPECT_TRUE(RectCalls(kPlateColor).empty());

        // Closed: the board gets its clicks again and the button works; Close of the list hides it
        const auto e2 = SquareCenter("e2");
        EXPECT_FALSE(CoreManager::GetUIInput().DispatchMouseButtonDown(Button(e2.first, e2.second, true)));
        EXPECT_TRUE(Model().HasSelection());
        ASSERT_TRUE(ClickButton(kMenuColor));
        EXPECT_EQ(log.LastAction(), "ChessOpenMenu"_intern);
        EXPECT_TRUE(MenuList()->_visible);
        ASSERT_TRUE(ClickMenuList("MenuClose").Both());
        EXPECT_EQ(log.LastAction(), "ChessCloseMenu"_intern);
        EXPECT_FALSE(MenuList()->_visible);
        EXPECT_FALSE(Panel()->_visible);
    }

    TEST_F(ChessUiTest, WhileThePanelIsShownThePlateTakesAPressAndTheDimClosesItAndNothingUnderTheDimSeesOne) {
        ASSERT_TRUE(OpenThePanel());
        Draw();
        ActionLog log;
        const uint64_t version = Model().GetVersion();
        ASSERT_TRUE(Play(Model(), "e2e4"));  // something for Reset to undo
        const uint64_t afterMove = Model().GetVersion();
        ASSERT_GT(afterMove, version);
        Draw();

        // On a segment (an action) and on the plate (its title): the panel stays
        const auto segment = ScreenCenter(PanelButton("EngineOwn"));
        const auto title = ScreenCenter(PanelElement("Title"));
        EXPECT_TRUE(ClickAtPixel(segment.first, segment.second).Both());
        EXPECT_EQ(log.Total(), 1u);
        EXPECT_EQ(log.LastAction(), PoolString::Intern(kSetEngineAction));
        EXPECT_TRUE(ClickAtPixel(title.first, title.second).Both());
        EXPECT_EQ(log.Total(), 1u) << "the title is no button";
        EXPECT_TRUE(Panel()->_visible);

        // On the dim over the board (a1, h8, below the plate), over the Reset button and over the Turn label: each press closes the panel and the list, nothing else gets it
        const UIElement& plate = PanelElement("Plate");
        const Pair<float, float> points[] = {SquareCenter("a1"), SquareCenter("h8"), ScreenCenter(*HudElement("Reset")), ScreenCenter(*HudElement("Turn"))};
        for (const auto& point : points) {
            SCOPED_TRACE(Format("{},{}", point.first, point.second).c_str());
            ASSERT_FALSE(Covers(plate, point)) << "a point of the dim, not of the plate";
            const size_t before = log.Total();
            EXPECT_TRUE(ClickAtPixel(point.first, point.second).Both());
            EXPECT_EQ(log.Total(), before + 1) << "one action: the dim's";
            EXPECT_EQ(log.LastAction(), "ChessCloseMenu"_intern);
            EXPECT_FALSE(Panel()->_visible);
            EXPECT_FALSE(MenuList()->_visible);
            EXPECT_FALSE(Model().HasSelection()) << "no click reached the board";
            EXPECT_EQ(Model().GetVersion(), afterMove) << "Reset under the dim was not pressed, the board did not move";
            EXPECT_EQ(Model().GetMoves().size(), 1u);

            ASSERT_TRUE(OpenThePanel());
            Draw();
        }
    }

    TEST_F(ChessUiTest, EverySegmentEmitsItsActionAndValueAndTheSelectionFollowsTheController) {
        CountingFactory factory({New<InstantEngine>()});
        Opponent().SetEngineFactory(factory.AsFactory());
        _manager->UpdateAll();
        ASSERT_TRUE(OpenThePanel());
        ActionLog log;
        const Vector<PanelSegment> segments = ExpectedPanelSegments();
        // A build without Stockfish hides its segment: nine are on the screen to click
        const auto IsShown = [](const PanelSegment& segment) { return kStockfishAvailable || segment.name != "EngineStockfish"; };

        // Every group in turn, with Computer picked before the color and the level; then back to two
        const auto clickGroup = [&](const char* group) {
            for (const PanelSegment& segment : segments) {
                if (StringView(segment.group) != group || !IsShown(segment)) {
                    continue;
                }
                SCOPED_TRACE(segment.name.c_str());
                const size_t before = log.Total();
                const Taken taken = ClickPanel(segment.name.c_str());
                EXPECT_TRUE(taken.Both());
                ASSERT_EQ(log.Total(), before + 1);
                EXPECT_EQ(log.LastAction(), PoolString::Intern(segment.action));
                EXPECT_EQ(log.LastValue(), PoolString::Intern(segment.id.c_str()));

                Draw();  // the HUD published the variable; the next draw moves the selection
                for (const PanelSegment& other : segments) {
                    if (StringView(other.group) == group && IsShown(other)) {
                        EXPECT_EQ(PanelButton(other.name.c_str()).selected, other.name == segment.name) << other.name.c_str();
                    }
                }
                if (StringView(group) == "ModeGroup") {
                    EXPECT_EQ(Opponent().GetPlays().has_value(), segment.id == "computer");
                } else if (StringView(group) == "ColorGroup") {
                    EXPECT_EQ(Opponent().GetPlays(), Optional<PieceColor>(segment.id == "white" ? PieceColor::Black : PieceColor::White)) << "the program plays the other color";
                } else if (StringView(group) == "EngineGroup") {
                    EXPECT_EQ(Opponent().GetEngine(), *FindOpponentEngine(segment.id));
                } else {
                    EXPECT_EQ(Opponent().GetLevel(), *FindOpponentLevel(segment.id));
                }
            }
        };
        clickGroup("EngineGroup");
        clickGroup("ModeGroup");  // ends with Computer
        ASSERT_TRUE(Opponent().GetPlays().has_value());
        clickGroup("ColorGroup");
        clickGroup("LevelGroup");
        ASSERT_TRUE(ClickPanel("ModeTwo").Both());
        Draw();
        EXPECT_FALSE(Opponent().GetPlays().has_value());
        EXPECT_TRUE(PanelButton("ModeTwo").selected);
        EXPECT_TRUE(PanelButton("LevelMax").selected) << "the level that was picked stays shown while two people play";
        EXPECT_TRUE(PanelButton("ColorBlack").selected) << "and so does the person's color";
        EXPECT_EQ(log.Total(), kStockfishAvailable ? 11u : 10u) << "every segment shown (ten, nine without Stockfish), and ModeTwo again";
    }

    TEST_F(ChessUiTest, AClickWithoutAnAnswerLeavesTheGroupAlone) {
        // The group owns no choice: it only reports. A pick the controller refuses leaves the segment that was selected selected
        CountingFactory failing(EngineError{EngineErrorKind::LaunchFailed, "no Stockfish in the test"});
        Opponent().SetEngineFactory(failing.AsFactory());
        _manager->UpdateAll();
        ASSERT_TRUE(OpenThePanel());
        Draw();
        ASSERT_TRUE(PanelButton("ModeTwo").selected);

        ASSERT_TRUE(ClickPanel("ModeComputer").Both());
        Draw();

        EXPECT_TRUE(PanelButton("ModeTwo").selected);
        EXPECT_FALSE(PanelButton("ModeComputer").selected);
    }

    TEST_F(ChessUiTest, NoGroupOfThePanelIsGreyedAndALevelAndAColorPickedForTwoPeopleApplyAtComputer) {
        // A greyed group read as a dead button, so every group takes clicks in every mode and the pick is remembered until Computer is picked
        CountingFactory working({New<InstantEngine>()});
        Opponent().SetEngineFactory(working.AsFactory());
        _manager->UpdateAll();
        ASSERT_TRUE(OpenThePanel());
        Draw();

        const auto expectNoGreying = [this](const char* state) {
            SCOPED_TRACE(state);
            for (const char* group : {"ModeGroup", "ColorGroup", "EngineGroup", "LevelGroup"}) {
                EXPECT_FALSE(PanelControl<UISegmented>(group).disabled) << group;
            }
            for (const char* name : {"ModeTwo", "ModeComputer", "ColorWhite", "ColorBlack", "EngineStockfish", "EngineOwn", "LevelEasy", "LevelMedium", "LevelHard", "LevelMax"}) {
                const UIElement& button = PanelButton(name);
                EXPECT_FALSE(button.groupDisabled) << name;
                EXPECT_EQ(button.GetStateLook().fill.a, 255) << name << " is drawn whole";
            }
        };
        expectNoGreying("two people play");

        // Two people play: a level and a color are picked on the drawn panel
        ActionLog log;
        const uint64_t revision = Opponent().GetRevision();
        ASSERT_TRUE(ClickPanel("LevelMax").Both());
        ASSERT_TRUE(ClickPanel("ColorBlack").Both());
        Draw();
        EXPECT_EQ(log.Total(), 2u) << "both clicks emitted their action";
        EXPECT_GT(Opponent().GetRevision(), revision);
        EXPECT_TRUE(PanelButton("LevelMax").selected);
        EXPECT_FALSE(PanelButton("LevelMedium").selected);
        EXPECT_TRUE(PanelButton("ColorBlack").selected);
        EXPECT_FALSE(PanelButton("ColorWhite").selected);
        EXPECT_TRUE(PanelButton("ModeTwo").selected);
        EXPECT_EQ(Var("opponentLevel"), "max");
        EXPECT_EQ(Var("opponentColor"), "black");
        EXPECT_EQ(ShownText(*HudElement("Menu")), "Menu");
        EXPECT_EQ(ControlOpponentString("level"), "max") << "get_state of the control API reads the remembered level";
        EXPECT_EQ(ControlOpponentString("plays"), "<missing>") << "null: nobody plays";
        EXPECT_EQ(working.Calls(), 0) << "nobody plays: nothing is asked for";
        expectNoGreying("two people play, a level and a color picked");

        // Computer applies both: the person has Black, so the program plays White at the level that was picked
        ASSERT_TRUE(ClickPanel("ModeComputer").Both());
        Draw();
        ASSERT_EQ(working.Calls(), 1);
        EXPECT_EQ(working.Levels()[0], "max");
        EXPECT_EQ(Opponent().GetPlays(), Optional<PieceColor>(PieceColor::White));
        EXPECT_EQ(ControlOpponentString("plays"), "white");
        EXPECT_EQ(ControlOpponentString("level"), "max");
        EXPECT_TRUE(PanelButton("LevelMax").selected);
        EXPECT_TRUE(PanelButton("ColorBlack").selected);
        EXPECT_EQ(ShownText(*HudElement("Menu")), "Menu");
        expectNoGreying("the program plays");

        // Back to two people: nothing grey, the choice still shown, and the groups still take clicks
        ASSERT_TRUE(ClickPanel("ModeTwo").Both());
        Draw();
        EXPECT_TRUE(PanelButton("LevelMax").selected);
        EXPECT_TRUE(PanelButton("ColorBlack").selected);
        expectNoGreying("two people again");
        const size_t total = log.Total();
        ASSERT_TRUE(ClickPanel("LevelEasy").Both());
        EXPECT_EQ(log.Total(), total + 1) << "a click in Two players mode is a pick, not a dead button";
        EXPECT_EQ(Opponent().GetLevel(), *FindOpponentLevel("easy"));
    }

    TEST_F(ChessUiTest, NoGroupOfThePanelIsGreyedWhateverTheControlApiSets) {
        // The control API picks the program's side without the panel and the groups stay whole while the choice follows it
        CountingFactory factory({New<InstantEngine>()});
        Opponent().SetEngineFactory(factory.AsFactory());
        _manager->UpdateAll();
        ASSERT_TRUE(OpenThePanel());  // a hidden canvas is not drawn, so its segments take the choice only once it is shown
        Draw();

        const auto wholePanel = [this] {
            for (const char* group : {"ModeGroup", "ColorGroup", "EngineGroup", "LevelGroup"}) {
                EXPECT_FALSE(PanelControl<UISegmented>(group).disabled) << group;
            }
        };
        wholePanel();

        ControlPlays("black");
        Draw();
        wholePanel();
        EXPECT_EQ(Var("opponentColor"), "white");
        EXPECT_TRUE(PanelButton("ColorWhite").selected);

        ControlPlays("white");
        Draw();
        wholePanel();
        EXPECT_EQ(Var("opponentColor"), "black");
        EXPECT_TRUE(PanelButton("ColorBlack").selected);

        ControlPlays(nullptr);
        Draw();
        wholePanel();
        EXPECT_TRUE(PanelButton("ModeTwo").selected);
        EXPECT_TRUE(PanelButton("ColorBlack").selected) << "the person's color is kept when the program leaves";
    }

    TEST_F(ChessUiTest, ARefusedStockfishPickOnThePanelKeepsComputerTheOwnEngineAndTheLevelAndTheProgramStillAnswers) {
        // A pick of an engine that cannot be had changes nothing, and the error is said
        SKIP_WITHOUT_STOCKFISH_SEGMENT();  // the pick of the name is the action's in that build: WithoutStockfishThePickOfTheNameOnTheActionIsRefused...
        Vector<String> asked;
        Opponent().SetEngineFactory([&asked](OpponentEngine engine, const OpponentLevel&) -> MadeEngine {
            const StringView id = OpponentEngineId(engine);
            asked.emplace_back(id.data(), id.size());
            if (engine == OpponentEngine::Stockfish) {
                return std::unexpected(EngineError{EngineErrorKind::LaunchFailed, "no Stockfish in the test"});
            }
            return New<InstantEngine>();
        });
        _manager->UpdateAll();
        ASSERT_TRUE(OpenThePanel());
        ASSERT_TRUE(ClickPanel("EngineOwn").Both());
        ASSERT_TRUE(ClickPanel("LevelHard").Both());
        ASSERT_TRUE(ClickPanel("ModeComputer").Both());
        Draw();
        ASSERT_EQ(Opponent().GetPlays(), Optional<PieceColor>(PieceColor::Black));
        ASSERT_EQ(asked.size(), 1u);

        ASSERT_TRUE(ClickPanel("EngineStockfish").Both());
        Draw();

        ASSERT_EQ(asked.size(), 2u);
        EXPECT_EQ(asked[1], "stockfish");
        EXPECT_TRUE(PanelButton("ModeComputer").selected) << "the mode does not move";
        EXPECT_FALSE(PanelButton("ModeTwo").selected);
        EXPECT_TRUE(PanelButton("EngineOwn").selected) << "the engine that plays stays picked";
        EXPECT_FALSE(PanelButton("EngineStockfish").selected);
        EXPECT_TRUE(PanelButton("LevelHard").selected);
        EXPECT_TRUE(PanelButton("ColorWhite").selected);
        EXPECT_EQ(PanelText("OpponentError"), "Engine not found");
        EXPECT_EQ(ShownText(Label("Status")), "Engine not found");
        EXPECT_EQ(ShownText(*HudElement("Menu")), "Menu");
        EXPECT_EQ(ControlOpponentString("engine"), "own");
        EXPECT_EQ(ControlOpponentString("plays"), "black");

        ASSERT_TRUE(CloseThePanel());
        ClickMove("e2", "e4");
        ASSERT_TRUE(FramesUntil([&] { return Model().GetMoves().size() == 2; })) << "the program that played before still answers";
        EXPECT_EQ(Model().GetPieceAt(Model().GetMoves()[1].To()).color, PieceColor::Black);
        EXPECT_EQ(asked.size(), 2u) << "no frame asked again";
        Draw();
        EXPECT_EQ(Var("opponentError"), "") << "the move of the person took the sentence away (the controller test pins that the program's own move does not)";
    }

    TEST_F(ChessUiTest, TheStockfishSegmentIsShownWhileStockfishIsOfferedAndSharesTheGroupWithTheOwnSegment) {
        // The fixture names a found Stockfish file; the scene file is the same in every build and on every machine
        ASSERT_TRUE(OpenThePanel());
        Draw();
        if constexpr (!kStockfishAvailable) {
            ExpectOnlyTheOwnSegment();
            return;
        }
        const UIElement& stockfish = PanelButton("EngineStockfish");
        const UIElement& own = PanelButton("EngineOwn");
        const UISegmented& group = PanelControl<UISegmented>("EngineGroup");
        EXPECT_TRUE(Opponent().OffersStockfish());
        EXPECT_TRUE(stockfish.active.visible);
        EXPECT_TRUE(own.active.visible);
        ASSERT_FALSE(own.rect.IsEmpty());
        EXPECT_NEAR(own.rect.h, kSegmentHeight, kPixelTolerance);
        ASSERT_FALSE(stockfish.rect.IsEmpty());
        EXPECT_NEAR(stockfish.rect.x, group.rect.x, kPixelTolerance) << "two segments share the group";
        EXPECT_NEAR(stockfish.rect.w, own.rect.w, kPixelTolerance);
        EXPECT_NEAR(own.rect.x + own.rect.w, group.rect.x + group.rect.w, kPixelTolerance);
    }

    TEST_F(ChessUiTest, TheStockfishSegmentIsHiddenWhenNoStockfishIsFoundAndTheOwnSegmentFillsTheGroupThen) {
        // In every build: no file found hides the segment the way a build without Stockfish does
        UseStockfishPath(MissingStockfishFile());
        ASSERT_FALSE(Opponent().OffersStockfish());
        EXPECT_EQ(Opponent().DefaultEngine(), OpponentEngine::Own);
        EXPECT_TRUE(Opponent().SetEngine(OpponentEngine::Own));
        _manager->UpdateAll();
        ASSERT_TRUE(OpenThePanel());
        Draw();
        ExpectOnlyTheOwnSegment();
    }

    TEST_F(ChessUiTest, AStockfishThatAppearsAfterTheFirstLookShowsTheSegmentWhenThePanelIsOpenedAgainNotBefore) {
        SKIP_WITHOUT_STOCKFISH_SEGMENT();
        const String path = MissingStockfishFile();
        UseStockfishPath(path);
        EXPECT_TRUE(Opponent().SetEngine(OpponentEngine::Own));
        ASSERT_TRUE(OpenThePanel());
        Draw();
        ExpectOnlyTheOwnSegment();
        ASSERT_TRUE(CloseThePanel());

        MakeStockfishFile(std::filesystem::path(path.c_str()));
        const uint64_t revision = Opponent().GetRevision();
        _manager->UpdateAll();
        Draw();
        EXPECT_FALSE(Opponent().OffersStockfish()) << "the cached look: no file lookup per frame";
        EXPECT_TRUE(PanelButton("EngineStockfish").rect.IsEmpty());

        ASSERT_TRUE(OpenThePanel());
        Draw();
        EXPECT_TRUE(Opponent().OffersStockfish());
        EXPECT_TRUE(PanelButton("EngineStockfish").active.visible);
        EXPECT_FALSE(PanelButton("EngineStockfish").rect.IsEmpty());
        EXPECT_EQ(Opponent().GetRevision(), revision) << "what is installed is not the state of the game";
        EXPECT_EQ(Opponent().GetEngine(), OpponentEngine::Own) << "a Stockfish found later only becomes pickable";
        ASSERT_TRUE(ClickPanel("EngineStockfish").Both());
        EXPECT_EQ(Var("opponentEngine"), "stockfish");

        std::error_code ignored;
        std::filesystem::remove(std::filesystem::path(path.c_str()), ignored);
        ASSERT_TRUE(CloseThePanel());
        ASSERT_TRUE(OpenThePanel());
        Draw();
        EXPECT_TRUE(PanelButton("EngineStockfish").rect.IsEmpty()) << "gone again: hidden at the next opening";
        EXPECT_EQ(Opponent().GetEngine(), OpponentEngine::Stockfish) << "the pick stays; nothing falls back";
    }

    TEST_F(ChessUiTest, WithAFactoryOfATestTheStockfishSegmentIsShownWhateverIsFound) {
        SKIP_WITHOUT_STOCKFISH_SEGMENT();
        UseStockfishPath(MissingStockfishFile());
        CountingFactory factory({New<InstantEngine>()});
        Opponent().SetEngineFactory(factory.AsFactory());
        ASSERT_TRUE(Opponent().OffersStockfish());
        ASSERT_TRUE(OpenThePanel());
        Draw();
        EXPECT_FALSE(PanelButton("EngineStockfish").rect.IsEmpty());
    }

    TEST_F(ChessUiTest, ThePanelWithTheStockfishSegmentHiddenPassesTheMatrixAndOwnEngineIsATouchTargetInTheNarrowestWindow) {
        // The layout rules for the group of one segment, in every build: the segment is hidden by hand here, so the desktop build holds the web one's panel too
        auto panel = Panel();
        ASSERT_TRUE(panel);
        panel->UpdateLayout(1280.0f, 720.0f);
        ASSERT_TRUE(panel->SetElementVisible("EngineStockfish"_intern, false));
        UICanvasLayout& layout = panel->GetLayout();

        const auto failures = ValidateWithWords(layout);
        EXPECT_TRUE(failures.empty()) << DescribeMatrixFailures(failures).c_str();
        const String problems = PanelProblemsOnMatrix(layout);
        EXPECT_TRUE(problems.empty()) << problems.c_str();

        LayOutInTheNarrowestWindow(layout);
        const UIElement& own = PanelButton("EngineOwn");
        const UIElement& group = PanelControl<UISegmented>("EngineGroup");
        EXPECT_TRUE(PanelButton("EngineStockfish").rect.IsEmpty());
        EXPECT_NEAR(own.rect.x, group.rect.x, kPixelTolerance);
        EXPECT_NEAR(own.rect.w, group.rect.w, kPixelTolerance);
        EXPECT_GE(std::min(own.rect.w, own.rect.h) * layout.GetResolved().scale, CoreManager::GetUIEnvironment().GetSettings().minTouchSize - kPixelTolerance);
        const UIContentFit fit = own.CheckContent(own.rect.w - 2.0f * kButtonAir, own.rect.h, layout.GetResolved().scale);
        EXPECT_TRUE(fit.hasContent && fit.fits) << "'" << ShownText(own).c_str() << "' in " << own.rect.w << " units";
        layout.Invalidate();
    }

#if CHESS_NO_STOCKFISH
    TEST_F(ChessUiTest, WithoutStockfishThePickOfTheNameOnTheActionIsRefusedAndTheOwnEngineStaysTheWayOnIsComputer) {
        // The default factory (no SetEngineFactory): the panel has no segment for the name, but the action can still carry it (a hand-made press)
        _manager->UpdateAll();
        EXPECT_EQ(Var("opponentEngine"), "own") << "a game starts with the own engine";

        PressWith(kSetEngineAction, "stockfish");

        EXPECT_EQ(Var("opponentEngine"), "own") << "refused as a whole, nothing falls back";
        EXPECT_EQ(Var("opponentError"), "Engine not supported here");
        EXPECT_EQ(Var("status"), "Engine not supported here");
        EXPECT_FALSE(Opponent().GetPlays().has_value());

        PressWith(kSetModeAction, "computer");

        EXPECT_EQ(Var("opponentMode"), "computer") << "the own engine plays";
        EXPECT_EQ(Var("opponentError"), "") << "a new pick: the person has seen the sentence";
        EXPECT_EQ(Opponent().GetEngine(), OpponentEngine::Own);
    }
#endif

    TEST_F(ChessUiTest, TheMenuButtonSaysMenuWhateverIsPickedAndThePanelNamesTheColors) {
        CountingFactory factory({New<InstantEngine>()});
        Opponent().SetEngineFactory(factory.AsFactory());
        _manager->UpdateAll();
        ASSERT_TRUE(OpenThePanel());
        EXPECT_EQ(ShownText(*HudElement("Menu")), "Menu");
        EXPECT_EQ(Opponent().GetPlays(), std::nullopt);

        ASSERT_TRUE(ClickPanel("ModeComputer").Both());
        Draw();
        EXPECT_EQ(ShownText(*HudElement("Menu")), "Menu") << "the button is not a state: the panel shows the pick";
        EXPECT_EQ(Opponent().GetPlays(), Optional<PieceColor>(PieceColor::Black)) << "the person has White, the program Black";
        EXPECT_EQ(Var("opponentColor"), "white");

        ASSERT_TRUE(ClickPanel("ColorBlack").Both());
        Draw();
        EXPECT_EQ(ShownText(*HudElement("Menu")), "Menu");
        EXPECT_EQ(Opponent().GetPlays(), Optional<PieceColor>(PieceColor::White));
        EXPECT_EQ(Var("opponentColor"), "black");

        ASSERT_TRUE(ClickPanel("ColorWhite").Both());
        Draw();
        EXPECT_EQ(ShownText(*HudElement("Menu")), "Menu");
        EXPECT_EQ(Opponent().GetPlays(), Optional<PieceColor>(PieceColor::Black));
        EXPECT_EQ(Var("opponentColor"), "white");

        ASSERT_TRUE(ClickPanel("ModeTwo").Both());
        Draw();
        EXPECT_EQ(ShownText(*HudElement("Menu")), "Menu");
        EXPECT_EQ(Opponent().GetPlays(), std::nullopt);
        EXPECT_EQ(Var("opponentMode"), "two");
    }

    TEST_F(ChessUiTest, ThePersonPlaysTheColorThePanelNamesAndTheProgramTheOther) {
        // The person plays the color the PANEL names (the Menu button names nothing)
        const auto engine = New<InstantEngine>();
        CountingFactory factory({engine});
        Opponent().SetEngineFactory(factory.AsFactory());
        _manager->UpdateAll();
        ASSERT_TRUE(OpenThePanel());
        ASSERT_TRUE(ClickPanel("ModeComputer").Both());
        ASSERT_TRUE(CloseThePanel());

        ClickMove("e2", "e4");
        ASSERT_TRUE(FramesUntil([&] { return Model().GetMoves().size() == 2; })) << "the person has White: the program answers with a black piece";
        EXPECT_EQ(Model().GetPieceAt(Model().GetMoves()[1].To()).color, PieceColor::Black);

        ASSERT_TRUE(OpenThePanel());
        ASSERT_TRUE(ClickPanel("ColorBlack").Both());
        ASSERT_TRUE(CloseThePanel());
        ASSERT_TRUE(FramesUntil([&] { return Model().GetMoves().size() == 3; })) << "the person has Black now: White is the program's and it is White's turn";
        EXPECT_EQ(Model().GetPieceAt(Model().GetMoves()[2].To()).color, PieceColor::White);
    }

    TEST_F(ChessUiTest, WhenNoStockfishIsFoundThePanelLeadsToTheOwnEngineAndTheProgramPlays) {
        // The action-level twin is WhenNoStockfishIsFoundTheEngineRowLeadsToTheOwnEngineAndComputerPlays. The
        // factory stands in for "no Stockfish found" and nothing else: it never makes the own engine for a Stockfish pick, so the program cannot have fallen back by itself
        Vector<String> asked;
        Opponent().SetEngineFactory([&asked](OpponentEngine engine, const OpponentLevel&) -> MadeEngine {
            const StringView id = OpponentEngineId(engine);
            asked.emplace_back(id.data(), id.size());
            if (engine == OpponentEngine::Stockfish) {
                return std::unexpected(EngineError{EngineErrorKind::LaunchFailed, "no Stockfish in the test"});
            }
            return New<InstantEngine>();
        });
        _manager->UpdateAll();
        StartFromTheStockfishName();
        ASSERT_TRUE(OpenThePanel());
        EXPECT_EQ(PanelText("OpponentError"), "") << "no error line to start with";

        ASSERT_TRUE(ClickPanel("ModeComputer").Both());
        Draw();
        EXPECT_EQ(PanelText("OpponentError"), "Engine not found") << "the panel says it where the person looks";
        EXPECT_EQ(ShownText(Label("Status")), "Engine not found");
        EXPECT_TRUE(PanelButton("ModeTwo").selected) << "the screen shows the truth: nobody plays";
        EXPECT_FALSE(PanelButton("ModeComputer").selected);
        if constexpr (kStockfishAvailable) {
            EXPECT_TRUE(PanelButton("EngineStockfish").selected) << "the pick failed and the choice is still Stockfish: no silent switch";
        }
        EXPECT_EQ(Var("opponentEngine"), "stockfish") << "no silent switch (the segment is hidden without Stockfish)";
        EXPECT_EQ(ShownText(*HudElement("Menu")), "Menu");
        ASSERT_EQ(asked.size(), 1u);
        EXPECT_EQ(asked[0], "stockfish");

        ASSERT_TRUE(ClickPanel("EngineOwn").Both());
        EXPECT_EQ(PanelText("OpponentError"), "") << "a new pick: the person has seen the sentence";
        EXPECT_TRUE(PanelButton("EngineOwn").selected);
        EXPECT_EQ(asked.size(), 1u) << "with nobody playing the choice only changes";

        ASSERT_TRUE(ClickPanel("ModeComputer").Both());
        Draw();
        EXPECT_TRUE(PanelButton("ModeComputer").selected);
        EXPECT_EQ(PanelText("OpponentError"), "");
        EXPECT_EQ(ShownText(*HudElement("Menu")), "Menu");
        ASSERT_EQ(asked.size(), 2u);
        EXPECT_EQ(asked[1], "own");

        ASSERT_TRUE(CloseThePanel());
        ClickMove("e2", "e4");
        ASSERT_TRUE(FramesUntil([&] { return Model().GetMoves().size() == 2; })) << "the own engine, behind the double, answers the person's move";
        EXPECT_EQ(Model().GetPieceAt(Model().GetMoves()[1].To()).color, PieceColor::Black);
        EXPECT_FALSE(Opponent().GetError().has_value());
    }

    TEST_F(ChessUiTest, ASearchErrorIsOnTheErrorLineAndAnyPickOfThePanelLetsThePersonOut) {
        // Twin of ChangingTheSideLetsThePersonOut... and ChangingTheLevelLetsThePersonOut... through the segments: the error line empties and the new engine is asked once
        const auto engine = New<FailingEngine>(EngineError{EngineErrorKind::LaunchFailed, "the program cannot be started"});
        CountingFactory factory({engine});
        ASSERT_NO_FATAL_FAILURE(ProvokeAnEngineError(factory));
        ASSERT_TRUE(OpenThePanel());
        EXPECT_EQ(PanelText("OpponentError"), "Engine not found") << "the first search failed";
        EXPECT_TRUE(PanelButton("ModeComputer").selected) << "the pick succeeded, only the search failed";

        ASSERT_TRUE(ClickPanel("LevelHard").Both());

        EXPECT_FALSE(Opponent().GetError().has_value());
        EXPECT_EQ(PanelText("OpponentError"), "");
        ASSERT_EQ(factory.Calls(), 2);
        EXPECT_EQ(factory.Levels()[1], "hard");
        ASSERT_TRUE(FramesUntil([&] { return Opponent().GetError().has_value(); })) << "black is still the program's and to move: the new engine is asked, and fails";
        IdleFrames();
        EXPECT_EQ(engine->Calls(), 2) << "once per act of the person, not per frame";

        ASSERT_TRUE(ClickPanel("ModeTwo").Both());  // nobody is the way out for good
        EXPECT_FALSE(Opponent().GetError().has_value());
        EXPECT_EQ(PanelText("OpponentError"), "");
    }

    TEST_F(ChessUiTest, TheSentencesOfTheErrorLineAreTheOnesOfTheOpponentText) {
        const auto engine = New<FailingEngine>(EngineError{EngineErrorKind::LaunchFailed, "the program cannot be started"});
        CountingFactory factory({engine});
        ASSERT_NO_FATAL_FAILURE(ProvokeAnEngineError(factory));
        ASSERT_TRUE(OpenThePanel());
        EXPECT_EQ(PanelText("OpponentError"), OpponentText::Error(*Opponent().GetError()));
        EXPECT_EQ(PanelText("OpponentError"), Var("status"));
    }

    // ---- the old cycle buttons are gone ------------------------------------------------------------------------------------------

    TEST_F(ChessUiTest, NoSideEngineOrLevelElementAndNoCycleActionIsLeftInTheScene) {
        // A leftover would be a dead button: the board no longer answers the cycle actions, and the words {level} and {engine} are no longer published
        pugi::xml_document document;
        ASSERT_TRUE(LoadShippedSceneDocument(document));
        EXPECT_TRUE(document.select_nodes("//element[@name='Side' or @name='Engine' or @name='Level']").empty()) << "the old HUD elements are gone, and nothing else takes their names";
        constexpr StringView kRemovedActions = "ChessCy"
                                               "cle";  // split, so that a search of the sources for the removed actions finds no leftover
        for (const pugi::xpath_node found : document.select_nodes("//element[@action]")) {
            const StringView action = found.node().attribute("action").value();
            EXPECT_EQ(action.find(kRemovedActions), StringView::npos) << found.node().attribute("name").value() << " emits " << ToStd(action);
            const bool known = action == "ChessReset" || action == "ChessOpenMenu" || action == "ChessOpenSection" || action == "ChessCloseMenu" || action == kSetModeAction ||
                               action == kSetColorAction || action == kSetEngineAction || action == kSetLevelAction || action == kPromoteAction || action == kCancelPromotionAction;
            EXPECT_TRUE(known) << found.node().attribute("name").value() << " emits " << ToStd(action) << ", which no component of the scene answers";
        }
        for (const pugi::xpath_node found : document.select_nodes("//element[@text]")) {
            const StringView text = found.node().attribute("text").value();
            EXPECT_TRUE(text.find("{level}") == StringView::npos && text.find("{engine}") == StringView::npos) << found.node().attribute("name").value() << ": " << ToStd(text);
        }

        // The same in the loaded scene: the HUD is Turn, Resign, Menu, Reset, Opening and Status, and the bottom left holds only the name of the opening (a label, never a button)
        const auto hud = Hud();
        ASSERT_TRUE(hud);
        size_t elements = 0;
        for (const auto& child : hud->GetRoot().children) {
            ++elements;
            if (child->style.rect.anchor == UIAnchor::BottomLeft) {
                EXPECT_EQ(child->name, "Opening"_intern) << "the bottom left of the HUD holds only the name of the opening";
                EXPECT_FALSE(child->IsInteractive());
            }
        }
        EXPECT_EQ(elements, 6u) << "Turn, Resign, Menu, Reset, Opening, Status";
        for (const char* name : {"Side", "Engine", "Level"}) {
            EXPECT_EQ(HudElement(name), nullptr) << name;
            EXPECT_EQ(Panel()->GetRoot().FindByName(PoolString::Intern(name)), nullptr) << name;
        }

        // And the variables of the old words are not published any more
        Draw();
        EXPECT_TRUE(CoreManager::GetUIVars().Get("level"_intern).empty());
        EXPECT_TRUE(CoreManager::GetUIVars().Get("engine"_intern).empty());
    }

    // ---- the panel is drawn over everything -----------------------------------------------------------------------------------------

    TEST_F(ChessUiTest, ThePanelIsDrawnOverTheHudAndTheResultBar) {
        const auto layerOf = [this](const char* name) {
            for (const auto& node : GetScene().GetRootNode().GetChildren()) {
                if (node->GetName() == PoolString::Intern(name)) {
                    return node->GetLayer();
                }
            }
            ADD_FAILURE() << name << " is not a node of the scene";
            return 0;
        };
        EXPECT_GT(layerOf("MenuPanel"), layerOf("Hud"));
        EXPECT_GT(layerOf("OpponentPanel"), layerOf("Hud"));
        EXPECT_GT(layerOf("Hud"), layerOf("Result"));

        // Drawn: a mate shows the bar of the result, then the panel is opened over it; the plate comes after the Reset button and the edge of the bar in the draw order
        ASSERT_TRUE(Play(Model(), "f2f3"));
        ASSERT_TRUE(Play(Model(), "e7e5"));
        ASSERT_TRUE(Play(Model(), "g2g4"));
        ASSERT_TRUE(Play(Model(), "d8h4"));
        ASSERT_TRUE(Model().IsGameOver());
        ASSERT_TRUE(OpenThePanel());
        Draw();
        const auto indexOf = [this](const Color& color) {
            for (size_t i = 0; i < _renderer->calls.size(); ++i) {
                if (_renderer->calls[i].kind == DrawKind::Rect && SameColor(_renderer->calls[i].color, color)) {
                    return i;
                }
            }
            return _renderer->calls.size();
        };
        const Color barEdge{205, 165, 60, 255};  // ChessResultComponent.cpp
        const size_t bar = indexOf(barEdge);
        const size_t reset = indexOf(kResetColor);
        const size_t plate = indexOf(kPlateColor);
        ASSERT_LT(bar, _renderer->calls.size()) << "the result bar is drawn once the game is over";
        ASSERT_LT(reset, _renderer->calls.size());
        ASSERT_LT(plate, _renderer->calls.size());
        EXPECT_LT(bar, plate) << "the plate is over the result bar";
        EXPECT_LT(reset, plate) << "and over the HUD";
    }

}  // namespace BECore::Chess::Tests
