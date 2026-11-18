// The corpus of every fuzz target of the game replayed, then mutated by the engine's deterministic mutator: the part of fuzzing that runs in every ctest on every platform. The shape is
// the engine's FuzzCorpusTest; tests/Fuzz/CLAUDE.md

#include <Common/Printers.h>
#include <Fuzz/FuzzMutator.h>
#include <tests/Fuzz/ChessFuzzTargets.h>

namespace BECore::Chess::Tests {

    using Fuzz::FuzzBytes;
    using Fuzz::FuzzRandom;
    using Fuzzing::ChessFuzzTarget;

    namespace {

        /// Mutations per target in an ordinary run, for a target of cost weight 1 (BAGI_FUZZ_ITERATIONS=<n> replaces it). A text parser of a position is cheap, so more than the engine's 50000
        /// keeps a case near two seconds in Debug
        constexpr uint64_t kDefaultIterations = 400000;

        /// The seed of the generator; mixed with the name of the target so the targets do not mutate the same way
        constexpr uint64_t kReplaySeed = 0x43484553535A5A31ull;

        constexpr uint64_t kFnvOffset = 0xCBF29CE484222325ull;
        constexpr uint64_t kFnvPrime = 0x100000001B3ull;

        /// Most mutations stacked on one seed
        constexpr uint32_t kMaxStackedMutations = 3;

        /// The committed seeds, tests/Fuzz/corpus of the game, found from the source tree like ShippedScene.h does
        std::filesystem::path CorpusRoot() {
            return std::filesystem::path(__FILE__).parent_path() / "Fuzz" / "corpus";
        }

        struct CorpusFile {
            String name;
            FuzzBytes bytes;
        };

        /// BAGI_FUZZ_ITERATIONS as it is, empty when it is not set (getenv is deprecated on Windows)
        String IterationsVariable() {
            String value;
#if defined(_WIN32)
            char* text = nullptr;
            size_t length = 0;
            if (_dupenv_s(&text, &length, "BAGI_FUZZ_ITERATIONS") == 0 && text != nullptr) {
                value = text;
            }
            std::free(text);
#else
            if (const char* text = std::getenv("BAGI_FUZZ_ITERATIONS")) {
                value = text;
            }
#endif
            return value;
        }

        uint64_t IterationCount(const ChessFuzzTarget& target) {
            const uint64_t byDefault = kDefaultIterations / std::max<uint32_t>(target.costWeight, 1);
            const String variable = IterationsVariable();
            if (variable.empty()) {
                return byDefault;
            }
            uint64_t value = 0;
            const char* end = variable.data() + variable.size();
            const auto result = std::from_chars(variable.data(), end, value);
            return (result.ec == std::errc() && result.ptr == end) ? value : byDefault;
        }

        uint64_t HashName(StringView name) {
            uint64_t value = kFnvOffset;
            for (const char symbol : name) {
                value = (value ^ static_cast<uint8_t>(symbol)) * kFnvPrime;
            }
            return value;
        }

        /// The files of a corpus folder in the order of their names (the order of a directory listing is not defined)
        Vector<CorpusFile> LoadCorpus(const std::filesystem::path& folder) {
            Vector<CorpusFile> files;
            std::error_code error;
            for (const auto& entry : std::filesystem::directory_iterator(folder, error)) {  //-NoStd: engine-file-layer: the corpus is read from the source tree
                if (!entry.is_regular_file()) {
                    continue;
                }
                std::ifstream stream(entry.path(), std::ios::binary);                                             //-NoStd: engine-file-layer
                std::string content((std::istreambuf_iterator<char>(stream)), std::istreambuf_iterator<char>());  //-NoStd: an eastl::string cannot be built from an istreambuf_iterator
                CorpusFile file;
                file.name = FromStdString(entry.path().filename().string());
                file.bytes.assign(reinterpret_cast<const uint8_t*>(content.data()), reinterpret_cast<const uint8_t*>(content.data()) + content.size());
                files.push_back(std::move(file));
            }
            std::sort(files.begin(), files.end(), [](const CorpusFile& a, const CorpusFile& b) { return a.name < b.name; });
            return files;
        }

        const ChessFuzzTarget& FindTarget(const String& name) {
            const Span<const ChessFuzzTarget> targets = Fuzzing::ChessFuzzTargetList();
            const auto found = std::find_if(targets.begin(), targets.end(), [&](const ChessFuzzTarget& target) { return name == target.name; });
            return *found;
        }

        struct FuzzCase {
            String name;
        };

        /// What the ctest line shows for the parameter, instead of the bytes of the struct
        void PrintTo(const FuzzCase& fuzzCase, std::ostream* stream) {
            *stream << fuzzCase.name;
        }

        Vector<FuzzCase> AllCases() {
            Vector<FuzzCase> cases;
            for (const ChessFuzzTarget& target : Fuzzing::ChessFuzzTargetList()) {
                cases.push_back({target.name});
            }
            return cases;
        }

        /// ctest names a case after this, so `ctest -R Fuzz` and `-R ChessFuzzReplayTest.*/Fen` find it
        std::string CaseName(const ::testing::TestParamInfo<FuzzCase>& info) {  //-NoStd: gtest's name generator returns a std::string
            return std::string(info.param.name.c_str());                        //-NoStd: gtest's name generator returns a std::string
        }

    }  // namespace

    class ChessFuzzReplayTest : public ::testing::TestWithParam<FuzzCase> {};

    TEST_P(ChessFuzzReplayTest, ReplaysTheCorpusAndMutationsOfIt) {
        const ChessFuzzTarget& target = FindTarget(GetParam().name);
        const std::filesystem::path folder = CorpusRoot() / target.corpusFolder;

        const Vector<CorpusFile> files = LoadCorpus(folder);
        ASSERT_FALSE(files.empty()) << "no seeds in " << folder.string();

        Vector<FuzzBytes> seeds;
        for (const CorpusFile& file : files) {
            EXPECT_LE(file.bytes.size(), Fuzz::kMaxFuzzInputSize) << file.name << " is larger than the inputs the fuzzers make";
            seeds.push_back(file.bytes);
            target.function(file.bytes.data(), file.bytes.size());
        }

        FuzzRandom random(kReplaySeed ^ HashName(target.name));
        const uint64_t iterations = IterationCount(target);
        for (uint64_t i = 0; i < iterations; ++i) {
            FuzzBytes input = seeds[i % seeds.size()];
            const uint32_t stacked = 1 + random.Below(kMaxStackedMutations);
            for (uint32_t step = 0; step < stacked; ++step) {
                Fuzz::Mutate(input, random, seeds);
            }
            target.function(input.data(), input.size());
        }
    }

    // A corpus that only the parser's refusals (or only its answers) reach never gets into the code after the accept
    TEST_P(ChessFuzzReplayTest, SeedsHoldAnInputTheParserTakesAndOneItRefuses) {
        const ChessFuzzTarget& target = FindTarget(GetParam().name);
        size_t taken = 0;
        size_t refused = 0;
        for (const CorpusFile& file : LoadCorpus(CorpusRoot() / target.corpusFolder)) {
            (target.accepts(file.bytes.data(), file.bytes.size()) ? taken : refused) += 1;
        }
        EXPECT_GT(taken, 0U) << "no seed of " << target.name << " that the parser takes";
        EXPECT_GT(refused, 0U) << "no seed of " << target.name << " that the parser refuses";
    }

    INSTANTIATE_TEST_SUITE_P(ChessFuzzCorpus, ChessFuzzReplayTest, ::testing::ValuesIn(AllCases()), CaseName);

    TEST(ChessFuzzCorpusTest, EveryFolderOfTheCorpusBelongsToATarget) {
        std::error_code error;
        size_t folders = 0;
        for (const auto& entry : std::filesystem::directory_iterator(CorpusRoot(), error)) {  //-NoStd: engine-file-layer
            if (!entry.is_directory()) {
                continue;
            }
            ++folders;
            const String folder = FromStdString(entry.path().filename().string());
            const Span<const ChessFuzzTarget> targets = Fuzzing::ChessFuzzTargetList();
            const bool owned = std::any_of(targets.begin(), targets.end(), [&](const ChessFuzzTarget& target) { return folder == target.corpusFolder; });
            EXPECT_TRUE(owned) << "tests/Fuzz/corpus/" << folder.c_str() << " has no target";
        }
        EXPECT_GT(folders, 0U) << "no corpus at " << CorpusRoot().string();
    }

    TEST(ChessFuzzCorpusTest, EveryTargetHasASeedFolder) {
        for (const ChessFuzzTarget& target : Fuzzing::ChessFuzzTargetList()) {
            EXPECT_FALSE(LoadCorpus(CorpusRoot() / target.corpusFolder).empty()) << "no seeds for " << target.name;
        }
    }

}  // namespace BECore::Chess::Tests
