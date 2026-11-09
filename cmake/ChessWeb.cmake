# =============================================================================
# Chess for the web (Emscripten): which files of the game go into the preload package
# =============================================================================
# Included by CMakeLists.txt and, alone, by tests/check_web_package.cmake (cmake -P), so the pure functions are checked on a desktop
# without an Emscripten SDK. The engine's be_web_preload_args (engine/cmake/BagiWeb.cmake) packs every file of config/ and assets/ of the
# game with no filter; a doc, a person's ChessSettings.local.xml (a Stockfish path) or an imgui.ini must not be served to others.
#
# Unverified: needs emsdk. emcc has --exclude-file <pattern> (a glob, fnmatch against the full path of a file as file_packager walks the
# folder), and the patterns below are written for that: a leading "*/" stands for the folders above a name. It applies to every
# --preload-file of the link, the engine's folders and the web overlay too (they hold none of these names). If emcc turns out to match
# otherwise, or to ignore the option for --preload-file, the fallback is a staged copy of config/ and assets/ for the package: a decision
# for slice 2 of the entry chess-web-build.
#
#   chess_web_exclude_patterns(OUT <var>)
#       The patterns of the files that stay out of the package.
#   chess_web_excluded(<relative path> OUT <var>)
#       TRUE when the path (relative to the game root, "/" as the separator, e.g. config/CLAUDE.md) matches a pattern, as emcc would match it.
#   chess_web_package_files(OUT <var> GAME_ROOT <dir>)
#       The files of <dir>/config and <dir>/assets that remain, as paths relative to <dir>, sorted.
include_guard(GLOBAL)

function(chess_web_exclude_patterns)
    cmake_parse_arguments(ARG "" "OUT" "" ${ARGN})
    if(NOT ARG_OUT)
        message(FATAL_ERROR "chess_web_exclude_patterns: OUT is required")
    endif()
    # Only "*", "?" and literal characters (a dot is literal): chess_web_excluded turns them into a regular expression
    set(${ARG_OUT}
        "*/CLAUDE.md"
        "*.md"
        "*.local.xml"
        "*/imgui.ini"
        "*/.DS_Store"
        "*.py"
        "*/__pycache__/*"
        PARENT_SCOPE)
endfunction()

function(chess_web_excluded PATH)
    cmake_parse_arguments(ARG "" "OUT" "" ${ARGN})
    if(NOT ARG_OUT)
        message(FATAL_ERROR "chess_web_excluded: OUT is required")
    endif()
    chess_web_exclude_patterns(OUT _patterns)
    # emcc matches the full path, so the relative one gets a leading "/" and "*/name" matches a file of any folder
    set(_subject "/${PATH}")
    set(_excluded FALSE)
    foreach(_pattern IN LISTS _patterns)
        # The glob as a regular expression, anchored: "." is literal, "*" any run (a "/" too, as fnmatch does), "?" one character
        string(REPLACE "." "\\." _regex "${_pattern}")
        string(REPLACE "*" ".*" _regex "${_regex}")
        string(REPLACE "?" "." _regex "${_regex}")
        if(_subject MATCHES "^${_regex}$")
            set(_excluded TRUE)
            break()
        endif()
    endforeach()
    set(${ARG_OUT} ${_excluded} PARENT_SCOPE)
endfunction()

function(chess_web_package_files)
    cmake_parse_arguments(ARG "" "OUT;GAME_ROOT" "" ${ARGN})
    if(NOT ARG_OUT OR NOT ARG_GAME_ROOT)
        message(FATAL_ERROR "chess_web_package_files: OUT and GAME_ROOT are required")
    endif()

    set(_files "")
    foreach(_folder config assets)
        # RELATIVE: the list is the same on every machine. Not CONFIGURE_DEPENDS (refused in script mode, and the check wants the list now)
        file(GLOB_RECURSE _found RELATIVE "${ARG_GAME_ROOT}" "${ARG_GAME_ROOT}/${_folder}/*")
        foreach(_file IN LISTS _found)
            chess_web_excluded("${_file}" OUT _excluded)
            if(NOT _excluded)
                list(APPEND _files "${_file}")
            endif()
        endforeach()
    endforeach()
    list(SORT _files)
    set(${ARG_OUT} "${_files}" PARENT_SCOPE)
endfunction()
