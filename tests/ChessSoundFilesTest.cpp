#include <BECore/Audio/AudioDecode.h>
#include <Chess/Sound/SoundCue.h>
#include <gtest/gtest.h>
#include <tests/ChessTestUtil.h>

/// The clips the cues name (Sound/SoundCue.h), read from the source tree and decoded the way the engine does; they are made by tools/make_sounds.py, which has its own --check
namespace BECore::Chess::Tests {

    namespace {

        constexpr uint32_t kSampleRate = 44100;
        constexpr float kMaxPeak = 0.95f;
        constexpr double kMinSeconds = 0.05;
        constexpr double kMaxSeconds = 2.0;
        constexpr double kEndSeconds = 0.002;  // the stretch at the end that has to be near silence: a tone cut off there clicks
        constexpr float kEndLevel = 0.02f;

        std::filesystem::path GameRoot() {
            return std::filesystem::path(__FILE__).parent_path().parent_path();
        }

        std::filesystem::path FileOfCue(SoundCue cue) {
            return GameRoot() / std::filesystem::path(ToStd(ClipPath(cue).ToStringView()));
        }

        Vector<uint8_t> BytesOfFile(const std::filesystem::path& path) {
            //-NoStd: file layer (engine-file-layer)
            std::ifstream file(path, std::ios::binary);
            Vector<uint8_t> bytes;
            for (std::istreambuf_iterator<char> it(file), end; it != end; ++it) {
                bytes.push_back(static_cast<uint8_t>(*it));
            }
            return bytes;
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
            ASSERT_TRUE(std::filesystem::exists(FileOfCue(cue))) << String(ClipPath(cue).ToStringView()).c_str();
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
        Vector<std::filesystem::path> named;
        for (const SoundCue cue : EnumUtils<SoundCue>::Values()) {
            named.push_back(FileOfCue(cue).lexically_normal());
        }
        const std::filesystem::path folder = GameRoot() / "assets" / "sounds";
        ASSERT_TRUE(std::filesystem::is_directory(folder));
        //-NoStd: file layer (engine-file-layer, engine-path-type)
        for (const auto& entry : std::filesystem::directory_iterator(folder)) {
            if (entry.path().extension() != ".wav") {
                continue;
            }
            const auto found = std::find(named.begin(), named.end(), entry.path().lexically_normal());
            EXPECT_NE(found, named.end()) << "no cue names " << entry.path().filename().string();
        }
    }

}  // namespace BECore::Chess::Tests
