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

# The own engine as a UCI program (uci/main.cpp): a console executable with no window and no engine services, built always.
# Its main is outside src/ because ChessModule globs src/*.cpp. Linked as the tests are, minus gtest
add_executable(BagiChessUci uci/main.cpp)
target_precompile_headers(BagiChessUci REUSE_FROM BECoreModule)
be_force_include_pch(BagiChessUci)
target_compile_definitions(BagiChessUci PRIVATE IMGUI_VULKAN_AVAILABLE=1 REFLECTION_GENERATED=1)
target_include_directories(BagiChessUci PRIVATE ${BAGI_ENGINE_DIR}/src ${CMAKE_BINARY_DIR})
be_link_engine(BagiChessUci)
target_link_libraries(BagiChessUci PRIVATE ChessModule SDLModule GamesModule)
if(APPLE)
    set_target_properties(BagiChessUci PROPERTIES
        BUILD_RPATH "@executable_path"
        INSTALL_RPATH "@executable_path"
    )
endif()
be_deploy_runtime_libs(BagiChessUci)

if(ENGINE_BUILD_TESTS)
    file(GLOB CHESS_TEST_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/tests/*.cpp")
    # WHOLE_ARCHIVE: ChessUiTest loads the shipped SceneConfig.xml, which names components no test references
    be_add_game_tests(ChessTests
        SOURCES ${CHESS_TEST_SOURCES}
        WHOLE_ARCHIVE ChessModule
    )
    # The interpreter that runs tests/fake_uci_engine.py, the stand-in for a UCI engine (the Python3 of BagiEngine.cmake, the one that
    # runs the smoke test below); an empty string makes the tests that start it skip themselves
    if(Python3_FOUND)
        target_compile_definitions(ChessTests PRIVATE "CHESS_TEST_PYTHON=\"${Python3_EXECUTABLE}\"")
    else()
        target_compile_definitions(ChessTests PRIVATE "CHESS_TEST_PYTHON=\"\"")
    endif()

    # Starts BagiChess with the control API and plays a few moves, a mate and a promotion over WebSocket
    be_add_game_control_tests(BagiChess SCRIPT ${CMAKE_CURRENT_SOURCE_DIR}/tests/control_smoke.py)

    # Starts BagiChessUci and talks UCI to it over pipes (handshake, a game, a clock, stop, quit); skipped (77) when the executable is not built
    if(Python3_FOUND)
        add_test(NAME BagiChessUciSmoke COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/uci_smoke.py --exe $<TARGET_FILE:BagiChessUci>)
        set_tests_properties(BagiChessUciSmoke PROPERTIES
            SKIP_RETURN_CODE 77
            TIMEOUT 120
            RUN_SERIAL TRUE
        )
    endif()
endif()
