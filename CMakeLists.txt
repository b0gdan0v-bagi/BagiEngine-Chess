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
        # One --exclude-file per pattern (emcc applies it to --preload-file folders: checked with emsdk 4.0.23 and the POST_BUILD check
        # below); SHELL: keeps the pair together, the quotes keep "*" from the shell
        target_link_options(BagiChess PRIVATE "SHELL:--exclude-file \"${_pattern}\"")
    endforeach()
    # What emcc really packed is read back after every link: a name of the patterns in the package, or a core file missing, fails the build
    add_custom_command(TARGET BagiChess POST_BUILD
        COMMAND ${CMAKE_COMMAND} -DGAME_ROOT=${CMAKE_CURRENT_SOURCE_DIR} -DPACKAGE_JS=$<TARGET_FILE_DIR:BagiChess>/BagiChess.js
            -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/check_web_package.cmake
        COMMENT "Checking the web package of BagiChess"
        VERBATIM)
endif()

# The own engine as a UCI program (uci/main.cpp): a console executable with no window and no engine services. Not on the web: a
# browser has no stdin / stdout to speak UCI on (and nothing there to start it). Its main is outside src/ because ChessModule globs
# src/*.cpp. Linked as the tests are, minus gtest. In a desktop ENGINE_SINGLE_THREAD build it is still built, but its `go` needs a
# worker Thread and asserts there (the smokes below start it); only the browser build, where it is not built, is meant to run single-thread
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
    # The fuzz targets of the parsers that read untrusted text, replayed by tests/ChessFuzzCorpusTest.cpp. The engine's BagiFuzzTargets is
    # built only inside the engine repository, so the game has a library of its own, made the way that one is. The engine's xorshift mutator
    # is one self-contained source, compiled in here so that the replay draws the same bytes on every platform
    file(GLOB CHESS_FUZZ_SOURCES
        "${CMAKE_CURRENT_SOURCE_DIR}/tests/Fuzz/*.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/tests/Fuzz/*.h"
    )
    add_library(ChessFuzzTargets STATIC ${CHESS_FUZZ_SOURCES} ${BAGI_ENGINE_DIR}/tests/Fuzz/FuzzMutator.cpp)
    source_group(TREE "${CMAKE_CURRENT_SOURCE_DIR}/tests/Fuzz" PREFIX "Fuzz" FILES ${CHESS_FUZZ_SOURCES})
    set_target_properties(ChessFuzzTargets PROPERTIES FOLDER "Tests")
    target_precompile_headers(ChessFuzzTargets REUSE_FROM BECoreModule)
    be_force_include_pch(ChessFuzzTargets)
    # IMGUI_VULKAN_AVAILABLE must match BECoreModule, otherwise the reused PCH is rejected
    target_compile_definitions(ChessFuzzTargets PRIVATE IMGUI_VULKAN_AVAILABLE=1 REFLECTION_GENERATED=1)
    # The game root, so that a header is <tests/Fuzz/ChessFuzzTargets.h> (<Fuzz/...> is the engine's folder)
    target_include_directories(ChessFuzzTargets PUBLIC ${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_BINARY_DIR})
    target_link_libraries(ChessFuzzTargets PUBLIC ChessModule)

    file(GLOB CHESS_TEST_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/tests/*.cpp")
    # WHOLE_ARCHIVE: ChessUiTest loads the shipped SceneConfig.xml, which names components no test references
    be_add_game_tests(ChessTests
        SOURCES ${CHESS_TEST_SOURCES}
        LINK ChessFuzzTargets
        WHOLE_ARCHIVE ChessModule
    )
    # Test helpers are included as <tests/ChessTestUtil.h>: be_add_game_tests puts this folder (the game root) on the include path of ChessTests
    # The interpreter that runs tests/fake_uci_engine.py, the stand-in for a UCI engine (the Python3 of BagiEngine.cmake, the one that
    # runs the smoke test below); an empty string makes the tests that start it skip themselves
    if(Python3_FOUND)
        target_compile_definitions(ChessTests PRIVATE "CHESS_TEST_PYTHON=\"${Python3_EXECUTABLE}\"")
    else()
        target_compile_definitions(ChessTests PRIVATE "CHESS_TEST_PYTHON=\"\"")
    endif()

    # Starts BagiChess with the control API and drives the opponent, analysis and rating scenarios over WebSocket
    be_add_game_control_tests(BagiChess SCRIPT ${CMAKE_CURRENT_SOURCE_DIR}/tests/control_smoke.py)
    # One ctest entry per recorded control scenario of tests/scenarios/ (BagiChessScenario.<file>), played on the manual clock
    be_add_game_scenario_tests(BagiChess DIR tests/scenarios)

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

        # Real games of Stockfish against BagiChess (the last group of tools/tests/test_stockfish.py, played by pytest); skipped (77) without
        # Stockfish, pytest or the executable
        add_test(NAME BagiChessStockfishGames COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/stockfish_games_smoke.py --exe $<TARGET_FILE:BagiChess>)
        set_tests_properties(BagiChessStockfishGames PROPERTIES
            SKIP_RETURN_CODE 77
            TIMEOUT 700
            RUN_SERIAL TRUE
        )

        # The unit tests of the match tool (standard library unittest, no Stockfish, no executable)
        add_test(NAME BagiChessMatchToolTests COMMAND ${Python3_EXECUTABLE} -m unittest discover -s ${CMAKE_CURRENT_SOURCE_DIR}/tools/tests -p "test_match_*.py" WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})
        set_tests_properties(BagiChessMatchToolTests PROPERTIES TIMEOUT 120)

        # The unit tests of tools/check_comment_only.py, the proof of every comment-only change (standard library unittest; each case builds a git repository in a temp folder, so git must be on PATH)
        add_test(NAME BagiChessCommentToolTests COMMAND ${Python3_EXECUTABLE} -m unittest discover -s ${CMAKE_CURRENT_SOURCE_DIR}/tools/tests -p "test_check_comment_only.py" WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})
        set_tests_properties(BagiChessCommentToolTests PROPERTIES TIMEOUT 120)

        # The game formats through the engine's layer (BECore::Format), not fmt:: (tests/check_fmt_layer.py runs the engine's codemod over src, tests and uci); skipped (77) without the engine's tool
        add_test(NAME BagiChessFmtLayerCheck COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/check_fmt_layer.py)
        set_tests_properties(BagiChessFmtLayerCheck PROPERTIES
            SKIP_RETURN_CODE 77
            TIMEOUT 60
        )

        # LICENSE and THIRD_PARTY_NOTICES.md hold to the tree (tests/check_notices.py): a row for every asset, the Stockfish section, no Stockfish file in the repository
        add_test(NAME BagiChessThirdPartyNotices COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/check_notices.py)
        set_tests_properties(BagiChessThirdPartyNotices PROPERTIES TIMEOUT 60)

        # A quoted include is allowed only for a source file's own header; every other include is <...> from an include root (the engine's
        # check_includes.py over the whole game: src, tests, uci)
        add_test(NAME BagiChessIncludeStyle COMMAND ${Python3_EXECUTABLE} ${BAGI_ENGINE_DIR}/CI/scripts/check_includes.py --root ${CMAKE_CURRENT_SOURCE_DIR})
        set_tests_properties(BagiChessIncludeStyle PROPERTIES TIMEOUT 60)

        # The game stays on the engine's type layer (check_types.py over the whole game); a real boundary says //-NoStd: <why>, there is no baseline file
        add_test(NAME BagiChessTypesCheck COMMAND ${Python3_EXECUTABLE} ${BAGI_ENGINE_DIR}/CI/scripts/check_types.py --root ${CMAKE_CURRENT_SOURCE_DIR})
        set_tests_properties(BagiChessTypesCheck PROPERTIES TIMEOUT 60)

        # PoolString constants are named, one text in one place (the engine's check_pool_strings.py over the whole game); the game has no exceptions, so no allowlist
        add_test(NAME BagiChessPoolStrings COMMAND ${Python3_EXECUTABLE} ${BAGI_ENGINE_DIR}/CI/scripts/check_pool_strings.py --root ${CMAKE_CURRENT_SOURCE_DIR})
        set_tests_properties(BagiChessPoolStrings PROPERTIES TIMEOUT 60)
    endif()

    # The pure part of the web package check (cmake/ChessWeb.cmake: the patterns on names, the files of the real tree, a scratch copy with
    # stray files and a new asset). Needs no emsdk and no list of files; the web build runs the real part after the link (above)
    add_test(NAME BagiChessWebPackageCheck COMMAND ${CMAKE_COMMAND}
        -DGAME_ROOT=${CMAKE_CURRENT_SOURCE_DIR} -DCHECK_DIR=${CMAKE_BINARY_DIR}/web-package-check
        -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/check_web_package.cmake)
    set_tests_properties(BagiChessWebPackageCheck PROPERTIES TIMEOUT 60)
endif()
