#include <Common/ContentFilesCheck.h>

/**
 * The content files of the game (`config/SceneConfig.xml` and any node file) are at the engine's content version. An engine update
 * that bumps `kContentVersion` fails here until `python engine/tools/migrate/migrate.py .` has rewritten them; the loader would
 * otherwise refuse the scene and start the game empty. Plain files read from the source tree, no engine services.
 */
namespace BECore::Tests {

    TEST(ChessContentVersionTest, EveryContentFileOfTheGameIsAtTheEnginesVersion) {
        const Path config = Path(__FILE__).Parent().Parent() / "config";
        ExpectContentFilesAtCurrentVersion(config);
    }

}  // namespace BECore::Tests
