# Chess
# Concrete IGame implementation (pure rules + scene components + ChessGame) and the BagiChess executable.

# Standalone build: the game in its own repository, the engine as the git submodule engine/.
# Inside the engine repository the engine is already set up by the root CMakeLists.txt and this block is skipped.
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    cmake_minimum_required(VERSION 3.31)
    project(BagiChess)
    include(${CMAKE_CURRENT_SOURCE_DIR}/engine/cmake/BagiEngine.cmake)
endif()

be_add_module(Chess LINK
    BECoreModule
    EventsModule
    GamesModule
    SDLModule
)
be_add_game_reflection(ChessModule)

be_add_game_executable(BagiChess
    GAME_ROOT ${CMAKE_CURRENT_SOURCE_DIR}
    MODULES ChessModule
)

if(ENGINE_BUILD_TESTS)
    file(GLOB CHESS_TEST_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/tests/*.cpp")
    # WHOLE_ARCHIVE: ChessUiTest loads the shipped SceneConfig.xml, which names components no test references
    be_add_game_tests(ChessTests
        SOURCES ${CHESS_TEST_SOURCES}
        WHOLE_ARCHIVE ChessModule
    )

    # Starts BagiChess with the control API and plays a few moves, a mate and a promotion over WebSocket
    be_add_game_control_tests(BagiChess SCRIPT ${CMAKE_CURRENT_SOURCE_DIR}/tests/control_smoke.py)
endif()
