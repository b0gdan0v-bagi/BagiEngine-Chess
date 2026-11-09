# Chess
# Concrete IGame implementation (pure rules + scene components + ChessGame) and the BagiChess executable.

# Standalone build: the game in its own repository, the engine as the git submodule engine/.
# Inside the engine repository the engine is already set up by the root CMakeLists.txt and this block is skipped.
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    cmake_minimum_required(VERSION 3.31)
    project(BagiChess)
    include(${CMAKE_CURRENT_SOURCE_DIR}/engine/cmake/BagiEngine.cmake)
endif()

option(CHESS_NO_STOCKFISH "No Stockfish: the opponent is the engine of our own only, no child process (the web build; also for a web-like desktop check)" OFF)
if(ENGINE_TARGET_WEB)
    # A browser has no child process: the switch is not a choice there
    set(CHESS_NO_STOCKFISH ON CACHE BOOL "No Stockfish: the opponent is the engine of our own only, no child process (forced ON for the web build)" FORCE)
endif()

be_add_module(Chess LINK
    BECoreModule
    EventsModule
    GamesModule
    SDLModule
)
# PUBLIC: ChessTests, BagiChess and BagiChessUci see the same value (UciProcess.h, OpponentEngines.h). Always 0 or 1, so `#if CHESS_NO_STOCKFISH` needs no `defined`
if(CHESS_NO_STOCKFISH)
    message(STATUS "Chess: no Stockfish (CHESS_NO_STOCKFISH=ON): the opponent is the own engine, no child process")
    target_compile_definitions(ChessModule PUBLIC CHESS_NO_STOCKFISH=1)
else()
    message(STATUS "Chess: Stockfish is available as the opponent (CHESS_NO_STOCKFISH=OFF)")
    target_compile_definitions(ChessModule PUBLIC CHESS_NO_STOCKFISH=0)
endif()
be_add_game_reflection(ChessModule)

be_add_game_executable(BagiChess
    GAME_ROOT ${CMAKE_CURRENT_SOURCE_DIR}
    MODULES ChessModule
)

# The files of the game that go into the preload package of the web build (cmake/ChessWeb.cmake); the engine's be_web_preload_args
# packs every file of config/ and assets/, so the docs and a person's local files are left out by the link options
include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/ChessWeb.cmake)
if(ENGINE_TARGET_WEB)
    chess_web_exclude_patterns(OUT CHESS_WEB_EXCLUDE_PATTERNS)
    foreach(_pattern IN LISTS CHESS_WEB_EXCLUDE_PATTERNS)
        # unverified: needs emsdk. One --exclude-file per pattern; SHELL: keeps the pair together, the quotes keep "*" from the shell
        target_link_options(BagiChess PRIVATE "SHELL:--exclude-file \"${_pattern}\"")
    endforeach()
endif()

# The own engine as a UCI program (uci/main.cpp): a console executable with no window and no engine services. Not on the web: a
# browser has no stdin / stdout to speak UCI on (and nothing there to start it). Its main is outside src/ because ChessModule globs
# src/*.cpp. Linked as the tests are, minus gtest
if(NOT ENGINE_TARGET_WEB)
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
endif()

# ChessTests, the control smoke and the checks below are behind ENGINE_BUILD_TESTS, which the web presets switch off
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

    if(Python3_FOUND)
        if(NOT ENGINE_TARGET_WEB)
            # Starts BagiChessUci and talks UCI to it over pipes (handshake, a game, a clock, stop, quit); skipped (77) when the executable is not built
            add_test(NAME BagiChessUciSmoke COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/uci_smoke.py --exe $<TARGET_FILE:BagiChessUci>)
            set_tests_properties(BagiChessUciSmoke PROPERTIES
                SKIP_RETURN_CODE 77
                TIMEOUT 120
                RUN_SERIAL TRUE
            )

            # Runs tools/uci_match.py for real: BagiChessUci against BagiChessUci under BagiChess as referee (no Stockfish); skipped (77) when an executable is missing
            add_test(NAME BagiChessMatchSmoke COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/match_smoke.py --game-exe $<TARGET_FILE:BagiChess> --uci-exe $<TARGET_FILE:BagiChessUci>)
            set_tests_properties(BagiChessMatchSmoke PROPERTIES
                SKIP_RETURN_CODE 77
                TIMEOUT 300
                RUN_SERIAL TRUE
            )
        endif()

        # The unit tests of the match tool (standard library unittest, no Stockfish, no executable)
        add_test(NAME BagiChessMatchToolTests COMMAND ${Python3_EXECUTABLE} -m unittest discover -s ${CMAKE_CURRENT_SOURCE_DIR}/tools/tests -p "test_match_*.py" WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})
        set_tests_properties(BagiChessMatchToolTests PROPERTIES TIMEOUT 120)
    endif()

    # The files of the preload package of the web build are exactly the game's (cmake/ChessWeb.cmake); a cmake -P script, no Python, no executable
    add_test(NAME BagiChessWebPackageCheck COMMAND ${CMAKE_COMMAND} -DGAME_ROOT=${CMAKE_CURRENT_SOURCE_DIR} -DCHECK_DIR=${CMAKE_CURRENT_BINARY_DIR}/web-package-check -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/check_web_package.cmake)
    set_tests_properties(BagiChessWebPackageCheck PROPERTIES TIMEOUT 60)
endif()
