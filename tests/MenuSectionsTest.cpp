#include <Chess/Game/MenuSections.h>
#include <Common/Printers.h>

/// The sections of the menu: pure data, nothing to start
namespace BECore::Chess::Tests {

    namespace {

        /// gtest cannot stream an EASTL string view
        String Text(StringView text) {
            return String(text.data(), text.size());
        }

    }  // namespace

    TEST(MenuSectionsTest, HoldsTheOpponentTheMovesTheGameTheSoundAndThePositionSectionsAndNothingElse) {
        const auto sections = MenuSections();
        ASSERT_EQ(sections.size(), 5u);
        EXPECT_EQ(Text(sections[0].id), "opponent");
        EXPECT_EQ(Text(sections[0].node), "OpponentPanel");
        EXPECT_EQ(Text(sections[0].label), "Opponent");
        EXPECT_EQ(Text(sections[1].id), "moves");
        EXPECT_EQ(Text(sections[1].node), "MovesSection");
        EXPECT_EQ(Text(sections[1].label), "Moves");
        EXPECT_EQ(Text(sections[2].id), "game");
        EXPECT_EQ(Text(sections[2].node), "GameSection");
        EXPECT_EQ(Text(sections[2].label), "Resign / Draw");
        EXPECT_EQ(Text(sections[3].id), "sound");
        EXPECT_EQ(Text(sections[3].node), "SoundPanel");
        EXPECT_EQ(Text(sections[3].label), "Sound");
        EXPECT_EQ(Text(sections[4].id), "position");
        EXPECT_EQ(Text(sections[4].node), "PositionSection");
        EXPECT_EQ(Text(sections[4].label), "Position");
    }

    TEST(MenuSectionsTest, IdsAreLowerCaseAndUnique) {
        const auto sections = MenuSections();
        for (size_t i = 0; i < sections.size(); ++i) {
            ASSERT_FALSE(sections[i].id.empty());
            for (const char c : sections[i].id) {
                EXPECT_TRUE(c >= 'a' && c <= 'z') << Text(sections[i].id);
            }
            for (size_t j = i + 1; j < sections.size(); ++j) {
                EXPECT_NE(sections[i].id, sections[j].id);
            }
        }
    }

    TEST(MenuSectionsTest, NodesAndWordsAreNamedAndUnique) {
        const auto sections = MenuSections();
        for (size_t i = 0; i < sections.size(); ++i) {
            ASSERT_FALSE(sections[i].node.empty());
            ASSERT_FALSE(sections[i].label.empty());
            EXPECT_TRUE(sections[i].label[0] >= 'A' && sections[i].label[0] <= 'Z') << Text(sections[i].label);
            for (size_t j = i + 1; j < sections.size(); ++j) {
                EXPECT_NE(sections[i].node, sections[j].node);
                EXPECT_NE(sections[i].label, sections[j].label);
            }
        }
    }

    TEST(MenuSectionsTest, FindsEverySectionByItsIdInTheOrderOfTheList) {
        const auto sections = MenuSections();
        for (const MenuSection& section : sections) {
            const auto found = FindMenuSection(section.id);
            ASSERT_TRUE(found.has_value()) << Text(section.id);
            EXPECT_EQ(found->id, section.id);
            EXPECT_EQ(found->node, section.node);
            EXPECT_EQ(found->label, section.label);
        }
        EXPECT_EQ(Text(sections.front().id), "opponent");
    }

    TEST(MenuSectionsTest, MatchingIsExact) {
        for (const char* text : {"", "Opponent", "OPPONENT", "opponen", "opponent ", " opponent", "OpponentPanel", "Moves", "move", "MovesSection", "Game", "GameSection", "Resign / Draw", "Sound",
                                 "sound ", "SoundPanel", "puzzles", "resign", "editor"}) {
            EXPECT_FALSE(FindMenuSection(text).has_value()) << "\"" << text << "\"";
        }
    }

}  // namespace BECore::Chess::Tests
