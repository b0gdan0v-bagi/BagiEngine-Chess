# Checks of the preload package of the web build (cmake/ChessWeb.cmake). Two modes, both print "OK" at the end; the first failed
# check stops with a FATAL_ERROR that names it.
#
# The pure part (ctest BagiChessWebPackageCheck; no emsdk, no list of files to edit when an asset is added):
#   cmake -DGAME_ROOT=<this repository> -DCHECK_DIR=<scratch folder> -P tests/check_web_package.cmake
#
# The real part (a POST_BUILD step of BagiChess in every web build): reads what the file packager wrote into the page's .js
#   cmake -DGAME_ROOT=<this repository> -DPACKAGE_JS=<build>/bin/BagiChess.js -P tests/check_web_package.cmake
cmake_minimum_required(VERSION 3.31)

if(NOT GAME_ROOT OR NOT (CHECK_DIR OR PACKAGE_JS))
    message(FATAL_ERROR "usage: cmake -DGAME_ROOT=<dir> (-DCHECK_DIR=<scratch dir> | -DPACKAGE_JS=<BagiChess.js>) -P check_web_package.cmake")
endif()

get_filename_component(GAME_ROOT "${GAME_ROOT}" ABSOLUTE)
include(${GAME_ROOT}/cmake/ChessWeb.cmake)

function(check_equal NAME ACTUAL EXPECTED)
    if(NOT "${ACTUAL}" STREQUAL "${EXPECTED}")
        message(FATAL_ERROR "${NAME}:\n  expected: ${EXPECTED}\n  actual:   ${ACTUAL}")
    endif()
endfunction()

# Every sprite of config/TextureLibrary.xml is drawn from its texturePath, so that file must be in the package whatever the patterns say. The
# source of a packed sprite (<atlas><source texturePath=...>) is only read by a repack in the editor and may stay out, so only the
# texturePath of a <sprite> itself counts. FILES are the game's files of the package, relative to the game root.
function(check_library_sprites WHERE)
    file(READ "${GAME_ROOT}/config/TextureLibrary.xml" _library)
    string(REGEX MATCHALL "<sprite[^>]*texturePath=\"[^\"]*\"" _sprites "${_library}")
    if(NOT _sprites)
        message(FATAL_ERROR "no <sprite texturePath=...> in config/TextureLibrary.xml: the library is empty or its format changed")
    endif()
    foreach(_sprite IN LISTS _sprites)
        string(REGEX REPLACE "^.*texturePath=\"([^\"]*)\"$" "\\1" _path "${_sprite}")
        if(NOT _path IN_LIST ARGN)
            message(FATAL_ERROR "${WHERE}: config/TextureLibrary.xml draws a sprite from ${_path}, which the web package lacks (a pattern of cmake/ChessWeb.cmake hides it?)")
        endif()
    endforeach()
endfunction()

# The files the game needs: without the first four it does not start, without the two atlases it draws no piece, without the opening table it
# plays and names no opening; a package that lacks one is broken, whatever else is in it
set(CORE_FILES
    config/ApplicationConfig.xml
    config/SceneConfig.xml
    config/ChessSettings.xml
    config/TextureLibrary.xml
    assets/chess_test.png
    assets/atlases/pieces.png
    assets/openings/openings.tsv)

if(PACKAGE_JS)
    if(NOT EXISTS "${PACKAGE_JS}")
        message(FATAL_ERROR "the web package script is not there: ${PACKAGE_JS}")
    endif()
    # file_packager writes one {filename:"<virtual path>",start:n,end:n} per file into loadPackage({files:[...]}) (the keys are quoted
    # or not, as the minifier of emcc leaves them); the virtual path starts with the layer's root (BE_WEB_ENGINE_ROOT, BE_WEB_GAME_ROOT, BE_WEB_WEB_ROOT)
    file(READ "${PACKAGE_JS}" _js)
    string(REGEX MATCHALL "\"?filename\"?: *\"[^\"]*\"" _entries "${_js}")
    if(NOT _entries)
        message(FATAL_ERROR "no \"filename\" entry in ${PACKAGE_JS}: the data package of the page is empty or its format changed")
    endif()
    set(_game_files "")
    foreach(_entry IN LISTS _entries)
        string(REGEX REPLACE "^\"?filename\"?: *\"([^\"]*)\"$" "\\1" _virtual "${_entry}")
        # Every layer of the package is held to the patterns: the link option is one for all of them
        chess_web_excluded("${_virtual}" OUT _excluded)
        if(_excluded)
            message(FATAL_ERROR "the web package holds ${_virtual}, a name that cmake/ChessWeb.cmake keeps out (is --exclude-file in the link options of BagiChess?)")
        endif()
        if(_virtual MATCHES "^/bagi/game/(.*)$")
            list(APPEND _game_files "${CMAKE_MATCH_1}")
        endif()
    endforeach()
    list(SORT _game_files)
    foreach(_core IN LISTS CORE_FILES)
        if(NOT _core IN_LIST _game_files)
            message(FATAL_ERROR "the web package lacks ${_core} of the game")
        endif()
    endforeach()
    # What the game's tree says the package takes is what the package holds: a file that stayed out by mistake or got in by mistake
    chess_web_package_files(OUT _tree_files GAME_ROOT "${GAME_ROOT}")
    check_equal("the game's files in the web package against the tree" "${_game_files}" "${_tree_files}")
    check_library_sprites("the web package" ${_game_files})
    list(LENGTH _entries _count)
    list(LENGTH _game_files _game_count)
    message(STATUS "web package: ${_count} files, ${_game_count} of the game, as the tree says")
    message("OK")
    return()
endif()

# --- the patterns on names alone: what a developer's folder may hold stays out, what the game ships stays in
foreach(_name
        config/CLAUDE.md config/README.md assets/notes.md
        config/ChessSettings.local.xml config/imgui.ini config/.DS_Store assets/.DS_Store
        assets/tool.py assets/__pycache__/tool.cpython-312.pyc config/sub/CLAUDE.md assets/sounds/move.artgen.json assets/artgen/WhiteKing.artgen.json
        assets/artgen/WhiteKing.png assets/artgen/sub/x.png)
    chess_web_excluded("${_name}" OUT _excluded)
    check_equal("excluded: ${_name}" "${_excluded}" "TRUE")
endforeach()
foreach(_name
        config/ApplicationConfig.xml config/ChessSettings.xml config/SceneConfig.xml config/TextureLibrary.xml
        assets/atlases/pieces.png assets/sprites/x.png config/localxml.xml assets/imgui.png config/nodes/ChessHud.xml assets/artgen.json)
    chess_web_excluded("${_name}" OUT _excluded)
    check_equal("not excluded: ${_name}" "${_excluded}" "FALSE")
endforeach()

# --- the package of the real tree: only files of config/ and assets/, the core files among them, nothing that a pattern keeps out.
# No list of files here: a new asset or node file changes nothing in this check
chess_web_package_files(OUT tree_files GAME_ROOT "${GAME_ROOT}")
foreach(_file IN LISTS tree_files)
    if(NOT _file MATCHES "^(config|assets)/")
        message(FATAL_ERROR "the package holds ${_file}, outside config/ and assets/")
    endif()
    chess_web_excluded("${_file}" OUT _excluded)
    if(_excluded)
        message(FATAL_ERROR "the package holds ${_file}, which a pattern keeps out")
    endif()
endforeach()
foreach(_core IN LISTS CORE_FILES)
    if(NOT _core IN_LIST tree_files)
        message(FATAL_ERROR "the package lacks ${_core} of the game")
    endif()
endforeach()

# The sprites the game draws are in the package; a pattern that would hide one fails here
check_library_sprites("the tree" ${tree_files})

# --- stray files of a developer's machine do not change the list. A scratch copy of the layout in CHECK_DIR (the checkout is not
# touched): the shipped names are the files of the real tree, so the check follows the assets of the game
get_filename_component(scratch "${CHECK_DIR}" ABSOLUTE)  # the glob of chess_web_package_files needs an absolute root
file(REMOVE_RECURSE "${scratch}")
foreach(_name ${tree_files})
    file(WRITE "${scratch}/${_name}" "x")
endforeach()
chess_web_package_files(OUT files GAME_ROOT "${scratch}")
check_equal("the scratch copy of the tree" "${files}" "${tree_files}")
foreach(_stray config/CLAUDE.md config/ChessSettings.local.xml config/imgui.ini assets/.DS_Store assets/__pycache__/a.pyc assets/sounds/move.artgen.json)
    file(WRITE "${scratch}/${_stray}" "x")
endforeach()
# A new asset enters the package by being there, no list to edit
file(WRITE "${scratch}/assets/sounds/zz_new_sound.wav" "x")
chess_web_package_files(OUT files GAME_ROOT "${scratch}")
set(expected ${tree_files} assets/sounds/zz_new_sound.wav)
list(SORT expected)
check_equal("the files with stray files and a new asset around" "${files}" "${expected}")
file(REMOVE_RECURSE "${scratch}")

message("OK")
