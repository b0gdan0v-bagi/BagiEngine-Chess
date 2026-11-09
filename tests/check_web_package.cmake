# Checks of cmake/ChessWeb.cmake, run by ctest as BagiChessWebPackageCheck:
#   cmake -DGAME_ROOT=<this repository> -DCHECK_DIR=<scratch folder> -P tests/check_web_package.cmake
# Prints "OK" at the end; the first failed check stops with a FATAL_ERROR that names it.
cmake_minimum_required(VERSION 3.31)

if(NOT GAME_ROOT OR NOT CHECK_DIR)
    message(FATAL_ERROR "usage: cmake -DGAME_ROOT=<dir> -DCHECK_DIR=<scratch dir> -P check_web_package.cmake")
endif()

include(${GAME_ROOT}/cmake/ChessWeb.cmake)

function(check_equal NAME ACTUAL EXPECTED)
    if(NOT "${ACTUAL}" STREQUAL "${EXPECTED}")
        message(FATAL_ERROR "${NAME}:\n  expected: ${EXPECTED}\n  actual:   ${ACTUAL}")
    endif()
endfunction()

# --- the patterns on names alone: what a developer's folder may hold stays out, what the game ships stays in
foreach(_name
        config/CLAUDE.md config/README.md assets/notes.md
        config/ChessSettings.local.xml config/imgui.ini config/.DS_Store assets/.DS_Store
        assets/tool.py assets/__pycache__/tool.cpython-312.pyc config/sub/CLAUDE.md)
    chess_web_excluded("${_name}" OUT _excluded)
    check_equal("excluded: ${_name}" "${_excluded}" "TRUE")
endforeach()
foreach(_name
        config/ApplicationConfig.xml config/ChessSettings.xml config/SceneConfig.xml config/TextureLibrary.xml
        assets/chess_test.png config/localxml.xml assets/imgui.png)
    chess_web_excluded("${_name}" OUT _excluded)
    check_equal("not excluded: ${_name}" "${_excluded}" "FALSE")
endforeach()

# --- the package is exactly the files of the game; a new config or asset file is a conscious change of this list
set(expected
    assets/chess_test.png
    config/ApplicationConfig.xml
    config/ChessSettings.xml
    config/SceneConfig.xml
    config/TextureLibrary.xml)
chess_web_package_files(OUT files GAME_ROOT "${GAME_ROOT}")
check_equal("the files of the preload package (a new file in config/ or assets/ must be added here, or excluded in cmake/ChessWeb.cmake)" "${files}" "${expected}")

# --- stray files of a developer's machine do not change the list (a scratch copy of the layout in CHECK_DIR, so the checkout is not touched)
set(scratch "${CHECK_DIR}")
file(REMOVE_RECURSE "${scratch}")
foreach(_name ${expected})
    file(WRITE "${scratch}/${_name}" "x")
endforeach()
foreach(_stray config/CLAUDE.md config/ChessSettings.local.xml config/imgui.ini assets/.DS_Store assets/__pycache__/a.pyc)
    file(WRITE "${scratch}/${_stray}" "x")
endforeach()
chess_web_package_files(OUT files GAME_ROOT "${scratch}")
check_equal("the files with stray files around" "${files}" "${expected}")
file(REMOVE_RECURSE "${scratch}")

message("OK")
