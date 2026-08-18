cmake_minimum_required(VERSION 3.28)

project(gigantima
    VERSION     0.1.0
    DESCRIPTION "Gigantima - an Ultima VI-style seamless-world RPG in C23 + SDL3"
    LANGUAGES   C)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")

# ---------------------------------------------------------------------------
# Options
# ---------------------------------------------------------------------------
option(GIGANTIMA_USE_SYSTEM_SDL "Use find_package(SDL3) instead of ext/sdl" OFF)
option(GIGANTIMA_TESTS          "Build the unit tests"                      ON)

# Warnings are errors, in **every** build type - not debug and CI alone.
#
# A warning that is only fatal somewhere else is a warning that gets committed
# here and discovered by someone else's build, and the whole value of a strict
# warning set is that it fires at the moment the mistake is made. The set
# includes -Wconversion and -Wsign-conversion, which are the two that actually
# catch defects in a codebase full of tile arithmetic and mixed int widths.
#
# This applies to first-party targets only; see cmake/CompilerWarnings.cmake.
# Turning it off is a deliberate local act, not the default:
#   cmake -B build -DGIGANTIMA_WERROR=OFF
option(GIGANTIMA_WERROR         "Treat compiler warnings as errors"         ON)
option(GIGANTIMA_ASAN           "Build with address+UB sanitizers"          OFF)
option(GIGANTIMA_EDITOR         "Build the level editor"                    ON)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE RelWithDebInfo CACHE STRING "" FORCE)
endif()

set(CMAKE_C_STANDARD 23)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)   # so clangd/IDEs see -std=c23

include(Platform)
include(CompilerWarnings)
include(Layering)

gg_check_layering()

# ---------------------------------------------------------------------------
# Dependencies
#
# SDL, SDL_image and SDL_mixer are pinned submodules under ext/ rather than
# FetchContent downloads, so a clone records the exact commit it builds
# against and an offline build works. See ext/README.md.
# ---------------------------------------------------------------------------
if(GIGANTIMA_USE_SYSTEM_SDL)
    find_package(SDL3 3.2 REQUIRED CONFIG)
    find_package(SDL3_image REQUIRED CONFIG)
    find_package(SDL3_mixer QUIET CONFIG)
    set(GG_SDL      SDL3::SDL3)
    set(GG_SDLIMAGE SDL3_image::SDL3_image)
    message(STATUS "gigantima: using system SDL3 ${SDL3_VERSION}")
else()
    if(NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/ext/sdl/CMakeLists.txt")
        message(FATAL_ERROR
            "ext/sdl is empty - the submodules are not checked out.\n"
            "  git submodule update --init --depth 1 ext/sdl ext/sdl_image\n"
            "Or configure with -DGIGANTIMA_USE_SYSTEM_SDL=ON to use an "
            "installed SDL3 instead.")
    endif()

    set(SDL_SHARED       OFF CACHE BOOL "" FORCE)
    set(SDL_STATIC       ON  CACHE BOOL "" FORCE)
    set(SDL_TEST_LIBRARY OFF CACHE BOOL "" FORCE)
    set(SDL_TESTS        OFF CACHE BOOL "" FORCE)
    set(SDL_EXAMPLES     OFF CACHE BOOL "" FORCE)
    set(SDL_INSTALL      OFF CACHE BOOL "" FORCE)

    # An Xbox pad speaks a vendor-specific protocol, so on Linux it needs
    # either the kernel's xpad or SDL talking to it through libusb. WSL2 has no
    # xpad, so without this the pad is invisible there. If libusb is missing at
    # configure time SDL simply turns it back off.
    if(UNIX AND NOT APPLE)
        set(SDL_HIDAPI_LIBUSB ON CACHE BOOL "" FORCE)
    endif()

    add_subdirectory(ext/sdl EXCLUDE_FROM_ALL)
    set(GG_SDL SDL3::SDL3-static)

    # SDL_image: PNG only. Its built-in decoder handles PNG with no external
    # libpng, and every other format it can build costs a dependency for a
    # file type this game does not ship.
    #
    # BUILD_SHARED_LIBS must be off *before* the add_subdirectory: SDL_image
    # picks its SDL target from it, and with it on it looks for
    # SDL3::SDL3-shared, which a static SDL build never creates. The failure
    # reads as "could not find a configuration file for package SDL3", which
    # points at the wrong thing entirely.
    set(BUILD_SHARED_LIBS  OFF CACHE BOOL "" FORCE)
    set(SDLIMAGE_VENDORED  OFF CACHE BOOL "" FORCE)
    set(SDLIMAGE_DEPS_SHARED OFF CACHE BOOL "" FORCE)
    set(SDLIMAGE_SAMPLES   OFF CACHE BOOL "" FORCE)
    set(SDLIMAGE_TESTS     OFF CACHE BOOL "" FORCE)
    set(SDLIMAGE_INSTALL   OFF CACHE BOOL "" FORCE)
    set(SDLIMAGE_BMP       ON  CACHE BOOL "" FORCE)
    set(SDLIMAGE_PNG       ON  CACHE BOOL "" FORCE)

    # PNG through stb_image rather than libpng. libpng drags in zlib, and a
    # system zlib whose CMake package advertises a static library it does not
    # ship fails the configure with an error that names neither - which is
    # exactly what happened on the development machine. stb_image has no
    # dependencies at all and decodes the 8-bit RGBA PNGs this game ships, so
    # all four target platforms build from the same source with nothing
    # installed. The cost is APNG support, which nothing here uses.
    set(SDLIMAGE_PNG_LIBPNG OFF CACHE BOOL "" FORCE)
    set(SDLIMAGE_PNG_SAVE   OFF CACHE BOOL "" FORCE)
    foreach(_fmt AVIF GIF JPG JXL LBM PCX PNM QOI SVG TGA TIF WEBP XCF XPM XV)
        set(SDLIMAGE_${_fmt} OFF CACHE BOOL "" FORCE)
    endforeach()

    add_subdirectory(ext/sdl_image EXCLUDE_FROM_ALL)
    set(GG_SDLIMAGE SDL3_image::SDL3_image-static)
    if(NOT TARGET ${GG_SDLIMAGE})
        set(GG_SDLIMAGE SDL3_image::SDL3_image)
    endif()

    message(STATUS "gigantima: SDL3 and SDL3_image from ext/ submodules")
endif()

# ---------------------------------------------------------------------------
# The core library
#
# Everything except the frontends' main.c, so the tests and the editor link
# exactly the code the game runs.
# ---------------------------------------------------------------------------
add_library(gigantima_core STATIC
    src/core/gg_ids.c
    src/core/gg_world.c
    src/core/gg_maptext.c
    src/core/gg_actor.c
    src/core/gg_path.c
    src/core/gg_game.c
    src/core/gg_dialogue.c
    src/core/gg_combat.c
    src/core/gg_magic.c
    src/core/gg_bestiary.c
    src/core/gg_quest.c
    src/core/gg_save.c
    src/core/gg_replay.c
    src/gfx/gg_font.c
    src/gfx/gg_render.c
    src/audio/gg_audio.c
    src/editor/gg_edit.c
    src/ui/gg_ui.c
    src/ui/gg_menu.c
    src/ui/gg_screens.c
    src/platform/gg_settings.c
    src/platform/gg_paths.c
    src/platform/gg_input.c
    src/debug/gg_debug.c)

# One include root, so every #include names the layer it crosses:
#   #include "core/gg_game.h"   not   #include "../core/gg_game.h"
target_include_directories(gigantima_core PUBLIC src)
target_link_libraries(gigantima_core PUBLIC ${GG_SDL} ${GG_SDLIMAGE})

# Where the art lives when running straight out of the build tree, so the
# ordinary edit-build-run loop needs no install step. See src/platform/gg_paths.c.
target_compile_definitions(gigantima_core PRIVATE
    GG_SOURCE_ASSETS="${CMAKE_CURRENT_SOURCE_DIR}/assets")

gg_configure(gigantima_core)

# ---------------------------------------------------------------------------
# Frontends
# ---------------------------------------------------------------------------
add_executable(gigantima src/frontend/game/main.c)
target_link_libraries(gigantima PRIVATE gigantima_core)
gg_configure(gigantima)

# Windows: GUI subsystem so no console window pops up. SDL_MAIN_USE_CALLBACKS
# in main.c supplies WinMain via SDL3/SDL_main.h.
set_target_properties(gigantima PROPERTIES
    WIN32_EXECUTABLE ON
    MACOSX_BUNDLE    OFF
    C_VISIBILITY_PRESET hidden)

if(GIGANTIMA_EDITOR AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/src/frontend/editor/main.c")
    add_executable(gigantima_editor src/frontend/editor/main.c)
    target_link_libraries(gigantima_editor PRIVATE gigantima_core)
    gg_configure(gigantima_editor)
    set_target_properties(gigantima_editor PROPERTIES C_VISIBILITY_PRESET hidden)
endif()

# ---------------------------------------------------------------------------
# Tests
# ---------------------------------------------------------------------------
if(GIGANTIMA_TESTS)
    enable_testing()
    add_executable(gigantima_tests tests/test_gigantima.c)
    target_link_libraries(gigantima_tests PRIVATE gigantima_core)
    gg_configure(gigantima_tests)

    add_test(NAME gigantima COMMAND gigantima_tests)
    # No window, no audio device, no gamepads: the simulation must not need any
    # of them, and this is what proves it.
    set_tests_properties(gigantima PROPERTIES
        ENVIRONMENT "SDL_VIDEODRIVER=dummy;SDL_AUDIODRIVER=dummy"
        TIMEOUT 120)
endif()

# ---------------------------------------------------------------------------
# Installing, and packaging
#
# **A package is a folder somebody unpacks and runs**, so the layout is the one
# `src/platform/gg_paths.c` already probes: the executables at the top and
# `assets/` beside them. The previous rules put the binary in `bin/` and the art
# in `share/gigantima/assets/`, which is the right shape for a distribution
# package and the wrong one for this game - it found neither, and started only
# because nobody had run the installed copy.
#
# The source TTF is excluded on purpose: the font is baked into
# `assets/atlas_font.png`, nothing reads the TTF at run time, and it carries its
# own licence.
# ---------------------------------------------------------------------------
set(GG_DOCS README.md LICENSE)

if(APPLE)
    # A bundle each, because a bundle is how macOS expects to be handed a
    # program, and each one carries its own assets in Contents/Resources - see
    # the probe in gg_paths.c. Three megabytes twice is a smaller price than an
    # editor that cannot find the art it edits.
    foreach(app gigantima gigantima_editor)
        set_target_properties(${app} PROPERTIES
            MACOSX_BUNDLE TRUE
            MACOSX_BUNDLE_BUNDLE_NAME          "${app}"
            MACOSX_BUNDLE_GUI_IDENTIFIER       "dev.gavin.${app}"
            MACOSX_BUNDLE_BUNDLE_VERSION       "${PROJECT_VERSION}"
            MACOSX_BUNDLE_SHORT_VERSION_STRING "${PROJECT_VERSION}"
            MACOSX_BUNDLE_COPYRIGHT            "GPL-3.0-or-later")
        install(TARGETS ${app} BUNDLE DESTINATION .)
        # Inside Resources/assets, not Resources itself: SDL_GetBasePath
        # returns a bundle's *resourcePath*, so the `<base>/assets` the probe
        # in gg_paths.c looks for is Contents/Resources/assets. Putting the art
        # straight into Resources would have been the obvious guess and the
        # wrong one.
        install(DIRECTORY assets/
                DESTINATION "${app}.app/Contents/Resources/assets"
                PATTERN "fonts/*.ttf" EXCLUDE)
    endforeach()
    install(FILES ${GG_DOCS} DESTINATION .)
else()
    install(TARGETS gigantima gigantima_editor RUNTIME DESTINATION .)
    install(DIRECTORY assets/ DESTINATION assets PATTERN "fonts/*.ttf" EXCLUDE)
    install(FILES ${GG_DOCS} DESTINATION .)
endif()

# ---------------------------------------------------------------------------
# CPack
# ---------------------------------------------------------------------------
# What a release calls itself. The project version is what the code is; this is
# what a *download* is, which is not always the same thing - a beta of 0.1.0 is
# still 0.1.0, and somebody with the file in their downloads folder needs to be
# able to tell. Set by the release job from the tag; "0.1.0" on a working tree.
set(GIGANTIMA_RELEASE "${PROJECT_VERSION}" CACHE STRING
    "Version the packages are named for, e.g. 0.1.0-beta.1")

set(CPACK_PACKAGE_NAME          "gigantima")
set(CPACK_PACKAGE_VENDOR        "Gavin Glynn")
set(CPACK_PACKAGE_VERSION       "${GIGANTIMA_RELEASE}")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "${PROJECT_DESCRIPTION}")
set(CPACK_PACKAGE_INSTALL_DIRECTORY   "Gigantima")
set(CPACK_RESOURCE_FILE_LICENSE "${CMAKE_CURRENT_SOURCE_DIR}/LICENSE")
set(CPACK_PACKAGE_CHECKSUM      SHA256)
# Stripped: the debug info is two thirds of the download and a bug report here
# is a seed, a turn count and a replay file rather than a stack trace off a
# player's machine. The build tree keeps its symbols either way.
set(CPACK_STRIP_FILES           ON)
set(CPACK_VERBATIM_VARIABLES    ON)

# The archive is named for what it holds and what it runs on, because an
# artifact called `gigantima-0.1.0.tar.gz` in a downloads folder beside three
# others is four files nobody can tell apart.
if(WIN32)
    set(GG_PLATFORM "windows-x86_64")
elseif(APPLE)
    set(GG_PLATFORM "macos-${CMAKE_SYSTEM_PROCESSOR}")
else()
    set(GG_PLATFORM "linux-${CMAKE_SYSTEM_PROCESSOR}")
endif()
set(CPACK_PACKAGE_FILE_NAME "gigantima-${GIGANTIMA_RELEASE}-${GG_PLATFORM}")

if(WIN32)
    # ZIP always, so there is an artifact even where NSIS is not installed, and
    # an installer beside it where it is.
    #
    # HINTS as well as the PATH: the Windows runner *has* NSIS and does not put
    # it on the PATH, so the first version of this quietly produced a zip and
    # no installer at all - which is exactly the kind of "worked, sort of" a
    # find_program with no hints buys you.
    set(CPACK_GENERATOR "ZIP")
    find_program(GG_MAKENSIS
        NAMES makensis
        HINTS "$ENV{ProgramFiles\(x86\)}/NSIS" "$ENV{ProgramFiles}/NSIS"
              "C:/Program Files (x86)/NSIS" "C:/Program Files/NSIS")
    if(GG_MAKENSIS)
        message(STATUS "gigantima: NSIS at ${GG_MAKENSIS}; an installer will be built")
        list(APPEND CPACK_GENERATOR "NSIS")
        set(CPACK_NSIS_PACKAGE_NAME "Gigantima")
        set(CPACK_NSIS_DISPLAY_NAME "Gigantima")
        set(CPACK_NSIS_MODIFY_PATH  OFF)
        set(CPACK_NSIS_ENABLE_UNINSTALL_BEFORE_INSTALL ON)
        # Start-menu entries through CPack's own list rather than through
        # hand-written NSIS script fragments: those need four levels of
        # backslash escaping and break in ways the generator does not explain.
        set(CPACK_PACKAGE_EXECUTABLES "gigantima;Gigantima"
                                      "gigantima_editor;Gigantima Editor")
        set(CPACK_CREATE_DESKTOP_LINKS "gigantima")
    else()
        message(STATUS "gigantima: no makensis; the Windows package will be a "
                       "zip and not an installer")
    endif()
elseif(APPLE)
    set(CPACK_GENERATOR "TGZ;DragNDrop")
    set(CPACK_DMG_VOLUME_NAME "Gigantima ${PROJECT_VERSION}")
else()
    set(CPACK_GENERATOR "TGZ")
endif()

include(CPack)
