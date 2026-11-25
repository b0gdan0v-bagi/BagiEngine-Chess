#include <BECore/Audio/AudioDecode.h>
#include <BECore/File/File.h>
#include <Chess/Sound/SoundCue.h>
#include <Common/TestFiles.h>
#include <gtest/gtest.h>
#include <tests/ChessTestUtil.h>

/// The clips the cues name (Sound/SoundCue.h), read from the source tree and decoded the way the engine does; they are generated (src/Chess/Sound/CLAUDE.md, "The clips"), and these checks are their
/// numbers
namespace BECore::Chess::Tests {

    namespace {

        constexpr uint32_t kSampleRate = 44100;
        constexpr float kMaxPeak = 0.95f;
        constexpr double kMinSeconds = 0.02;  // a tick of a button is shorter than a knock
        constexpr double kMaxSeconds = 2.0;
        constexpr double kEndSeconds = 0.002;  // the stretch at the end that has to be near silence: a tone cut off there clicks
        constexpr float kEndLevel = 0.02f;

        Path GameRoot() {
            return Path(__FILE__).Parent().Parent();
        }

        Path FileOfCue(SoundCue cue) {
            return GameRoot() / ClipPath(cue).ToStringView();
        }

        Vector<uint8_t> BytesOfFile(const Path& path) {
            return ::BECore::Tests::ReadFileBytes(path);
        }

        AudioDecodeResult DecodeCue(SoundCue cue) {
            const Vector<uint8_t> bytes = BytesOfFile(FileOfCue(cue));
            return DecodeAudio(Span<const uint8_t>(bytes.data(), bytes.size()));
        }

        String NameOf(SoundCue cue) {
            return String(EnumUtils<SoundCue>::ToString(cue));
        }

    }  // namespace

    TEST(ChessSoundFilesTest, EveryCueHasAClipThatDecodes) {
        for (const SoundCue cue : EnumUtils<SoundCue>::Values()) {
            SCOPED_TRACE(NameOf(cue).c_str());
            ASSERT_TRUE(::BECore::Tests::PathExists(FileOfCue(cue))) << String(ClipPath(cue).ToStringView()).c_str();
            const AudioDecodeResult decoded = DecodeCue(cue);
            ASSERT_TRUE(decoded.has_value()) << (decoded.has_value() ? "" : decoded.error().c_str());
        }
    }

    TEST(ChessSoundFilesTest, EveryClipIsMonoAtTheRateOfTheOthers) {
        for (const SoundCue cue : EnumUtils<SoundCue>::Values()) {
            SCOPED_TRACE(NameOf(cue).c_str());
            const AudioDecodeResult decoded = DecodeCue(cue);
            ASSERT_TRUE(decoded.has_value());
            EXPECT_EQ(decoded->format.channels, 1);
            EXPECT_EQ(decoded->format.sampleRate, kSampleRate);
        }
    }

    TEST(ChessSoundFilesTest, EveryClipSoundsButNeverClipsAndHasAReasonableLength) {
        for (const SoundCue cue : EnumUtils<SoundCue>::Values()) {
            SCOPED_TRACE(NameOf(cue).c_str());
            const AudioDecodeResult decoded = DecodeCue(cue);
            ASSERT_TRUE(decoded.has_value());
            float peak = 0.0f;
            for (const float sample : decoded->samples) {
                peak = std::max(peak, std::abs(sample));
            }
            EXPECT_GT(peak, 0.0f) << "silent";
            EXPECT_LE(peak, kMaxPeak) << "at the master level 1 a louder clip clips";
            const double seconds = static_cast<double>(decoded->GetFrameCount()) / static_cast<double>(decoded->format.sampleRate);
            EXPECT_GE(seconds, kMinSeconds);
            EXPECT_LE(seconds, kMaxSeconds);
        }
    }

    TEST(ChessSoundFilesTest, EveryClipEndsInSilenceSoNoToneIsCutOff) {
        for (const SoundCue cue : EnumUtils<SoundCue>::Values()) {
            SCOPED_TRACE(NameOf(cue).c_str());
            const AudioDecodeResult decoded = DecodeCue(cue);
            ASSERT_TRUE(decoded.has_value());
            const size_t tail = static_cast<size_t>(kEndSeconds * static_cast<double>(decoded->format.sampleRate));
            ASSERT_GT(decoded->samples.size(), tail);
            for (size_t index = decoded->samples.size() - tail; index < decoded->samples.size(); ++index) {
                ASSERT_LE(std::abs(decoded->samples[index]), kEndLevel) << "sample " << index;
            }
        }
    }

    TEST(ChessSoundFilesTest, TheFolderHoldsNoClipThatNoCueNames) {
        Vector<Path> named;
        for (const SoundCue cue : EnumUtils<SoundCue>::Values()) {
            named.push_back(FileOfCue(cue).LexicallyNormal());
        }
        const Path folder = GameRoot() / "assets" / "sounds";
        ASSERT_TRUE(::BECore::Tests::IsFolder(folder));
        const auto walked = WalkDirectory(folder.View(), WalkMode::Folder, [&](StringView name, FileKind kind) {
            if (kind != FileKind::File || Path(name).Extension().View() != ".wav") {
                return true;
            }
            const Path entry = (folder / name).LexicallyNormal();
            const auto found = eastl::find(named.begin(), named.end(), entry);
            EXPECT_NE(found, named.end()) << "no cue names " << Path(name).CStr();
            return true;
        });
        EXPECT_TRUE(walked.has_value());
    }

}  // namespace BECore::Chess::Tests
