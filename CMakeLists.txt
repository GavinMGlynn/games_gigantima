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
    src/core/gg_actor.c
    src/core/gg_path.c
    src/core/gg_game.c
    src/core/gg_dialogue.c
    src/core/gg_combat.c
    src/core/gg_magic.c
    src/core/gg_bestiary.c
    src/core/gg_save.c
    src/gfx/gg_font.c
    src/gfx/gg_render.c
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

install(TARGETS gigantima RUNTIME DESTINATION bin)
install(DIRECTORY assets/ DESTINATION share/gigantima/assets
        PATTERN "fonts/*.ttf" EXCLUDE)
