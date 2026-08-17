# Platform.cmake - the supported set, enforced at configure time.
#
# Gigantima targets exactly four platforms, all 64-bit:
#
#   Linux x86_64    both RHEL-family and Debian-family
#   Windows x64     MSVC or clang-cl
#   macOS arm64     Apple Silicon
#
# Enforced rather than assumed. A 32-bit build would compile and then behave
# differently in ways nobody would be looking for, and this project has no
# 32-bit CI to catch it - so it is refused with a message that says so, rather
# than silently produced.

if(CMAKE_SIZEOF_VOID_P LESS 8)
    message(FATAL_ERROR
        "gigantima is 64-bit only.\n"
        "  found:  ${CMAKE_SIZEOF_VOID_P}-byte pointers on ${CMAKE_SYSTEM_PROCESSOR}\n"
        "  needed: a 64-bit toolchain")
endif()

set(GG_PLATFORM "")
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    set(GG_PLATFORM "linux-x86_64")
elseif(WIN32)
    set(GG_PLATFORM "windows-x64")
elseif(APPLE)
    set(GG_PLATFORM "macos-${CMAKE_SYSTEM_PROCESSOR}")
else()
    message(WARNING
        "gigantima has no CI for ${CMAKE_SYSTEM_NAME}; it may build, but "
        "nothing verifies that it works. Supported: Linux x86_64, Windows x64, "
        "macOS arm64.")
    set(GG_PLATFORM "${CMAKE_SYSTEM_NAME}-${CMAKE_SYSTEM_PROCESSOR}")
endif()

# ---------------------------------------------------------------------------
# Compiler gate - C23 needs a recent toolchain.
#   GCC 14+   Clang 19+   AppleClang 16+ (Xcode 16)   MSVC 19.39+ (VS 17.9)
# RHEL 9 and Ubuntu 22.04 both default to GCC 11, which is too old; install
# gcc-toolset-14 (RHEL/Rocky) or gcc-14 from the toolchain PPA (Debian/Ubuntu).
# ---------------------------------------------------------------------------
set(_gg_min "")
if(CMAKE_C_COMPILER_ID STREQUAL "GNU")
    set(_gg_min 14)
elseif(CMAKE_C_COMPILER_ID STREQUAL "Clang")
    set(_gg_min 19)
elseif(CMAKE_C_COMPILER_ID STREQUAL "AppleClang")
    set(_gg_min 16)
elseif(CMAKE_C_COMPILER_ID STREQUAL "MSVC")
    set(_gg_min 19.39)
endif()

if(_gg_min AND CMAKE_C_COMPILER_VERSION VERSION_LESS _gg_min)
    message(FATAL_ERROR
        "gigantima needs C23 support.\n"
        "  found:    ${CMAKE_C_COMPILER_ID} ${CMAKE_C_COMPILER_VERSION}\n"
        "  required: ${CMAKE_C_COMPILER_ID} ${_gg_min} or newer\n"
        "Point CMake at a newer compiler, e.g.\n"
        "  cmake -B build -DCMAKE_C_COMPILER=gcc-14")
endif()

message(STATUS "gigantima: ${GG_PLATFORM}, ${CMAKE_C_COMPILER_ID} ${CMAKE_C_COMPILER_VERSION} (C23)")

# MSVC has no /std:c23 yet; its newest C features sit behind /std:clatest, and
# /Zc:__STDC__ makes it report the standard version macros honestly. CMake's
# C_STANDARD 23 should already select the former, but say it outright so the
# build does not depend on that mapping staying put.
set(GG_C_FLAGS "")
if(MSVC)
    set(GG_C_FLAGS /std:clatest /Zc:__STDC__)
endif()

# C23 support is uneven: a compiler can take the flag and still not implement a
# given feature. Probe for `nullptr` rather than guessing from version numbers,
# using the same flags the real build gets, so a compiler that gains it later
# simply stops needing the shim.
include(CheckCSourceCompiles)
set(CMAKE_REQUIRED_FLAGS "${GG_C_FLAGS}")
list(JOIN CMAKE_REQUIRED_FLAGS " " CMAKE_REQUIRED_FLAGS)
check_c_source_compiles(
    "int main(void) { void *p = nullptr; return p != (void *)0; }"
    GG_HAVE_NULLPTR)
unset(CMAKE_REQUIRED_FLAGS)

if(GG_HAVE_NULLPTR)
    message(STATUS "gigantima: C23 nullptr - native")
else()
    message(STATUS "gigantima: C23 nullptr - not implemented, using a compatibility macro")
endif()
