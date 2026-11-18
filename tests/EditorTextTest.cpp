#include <Chess/Game/EditorText.h>
#include <tests/ChessTestUtil.h>

namespace BECore::Chess::Tests {

    TEST(EditorTextTest, EveryFenErrorHasAShortLine) {
        for (const FenError error : EnumUtils<FenError>::Values()) {
            const StringView line = EditorText::Refusal(error);
            EXPECT_FALSE(line.empty()) << EnumUtils<FenError>::ToString(error).data();
            EXPECT_LE(line.size(), EditorText::kMaxRefusalChars) << EnumUtils<FenError>::ToString(error).data();
        }
    }

    TEST(EditorTextTest, EveryEditRefusalHasAShortLine) {
        for (const EditRefusal refusal : EnumUtils<EditRefusal>::Values()) {
            const StringView line = EditorText::Refusal(refusal);
            EXPECT_FALSE(line.empty()) << EnumUtils<EditRefusal>::ToString(refusal).data();
            EXPECT_LE(line.size(), EditorText::kMaxRefusalChars) << EnumUtils<EditRefusal>::ToString(refusal).data();
        }
    }

    TEST(EditorTextTest, EveryClipboardStatusHasAShortDistinctLine) {
        Vector<StringView> seen;
        for (const ClipboardStatus status : EnumUtils<ClipboardStatus>::Values()) {
            const StringView line = EditorText::Clipboard(status);
            EXPECT_FALSE(line.empty()) << ToString(status).data();
            EXPECT_LE(line.size(), EditorText::kMaxRefusalChars) << ToString(status).data() << ": it is shown in the HUD's Turn line too";
            EXPECT_EQ(std::count(seen.begin(), seen.end(), line), 0) << ToString(status).data();
            seen.push_back(line);
        }
    }

    // The limit counts characters, which are bytes only while the lines are ASCII
    TEST(EditorTextTest, TheLinesAreAscii) {
        for (const FenError error : EnumUtils<FenError>::Values()) {
            for (const char c : EditorText::Refusal(error)) {
                EXPECT_GE(c, ' ');
                EXPECT_LT(c, 0x7f);
            }
        }
        for (const EditRefusal refusal : EnumUtils<EditRefusal>::Values()) {
            for (const char c : EditorText::Refusal(refusal)) {
                EXPECT_GE(c, ' ');
                EXPECT_LT(c, 0x7f);
            }
        }
        for (const ClipboardStatus status : EnumUtils<ClipboardStatus>::Values()) {
            for (const char c : EditorText::Clipboard(status)) {
                EXPECT_GE(c, ' ');
                EXPECT_LT(c, 0x7f);
            }
        }
    }

    TEST(EditorTextTest, TextErrorsAreOneLineAndRuleErrorsKeepTheirOwn) {
        EXPECT_EQ(EditorText::Refusal(FenError::WrongFieldCount), EditorText::Refusal(FenError::BadClock));
        EXPECT_EQ(EditorText::Refusal(FenError::BadPiecePlacement), "Not a position");
        EXPECT_EQ(EditorText::Refusal(FenError::BadKingCount), "Needs one king each");
        EXPECT_NE(EditorText::Refusal(FenError::PawnOnBackRank), EditorText::Refusal(FenError::OpponentInCheck));
    }

}  // namespace BECore::Chess::Tests
