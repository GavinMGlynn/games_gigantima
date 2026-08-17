# Layering.cmake - the src/ layer boundary, checked rather than remembered.
#
# The rule: **`src/core/` is the simulation and must not know how anything is
# drawn, heard, or typed at.** It may use SDL for portability - memory, files,
# logging, string formatting - but never a renderer, a texture, a window, an
# event or a font.
#
# That is what keeps the simulation headless: the unit tests run it with no
# video driver, the level editor runs the same code the game does, and a
# re-bake of the art cannot change the behaviour of a single turn. It is an
# easy rule to break by accident with one convenient #include, and the damage
# is only discovered much later, so it is enforced at configure time.
#
# `src/core/gg_ids.h` is the deliberate seam: generated from the art, but
# carrying only the vocabulary - which tile, which prop, which actor - and no
# pixel coordinates. Those live in `src/gfx/gg_atlas.h`, which core never sees.

function(gg_check_layering)
    file(GLOB_RECURSE _core_sources
         "${CMAKE_CURRENT_SOURCE_DIR}/src/core/*.c"
         "${CMAKE_CURRENT_SOURCE_DIR}/src/core/*.h")

    set(_violations "")
    foreach(_f ${_core_sources})
        file(STRINGS "${_f}" _bad REGEX "^[ \t]*#include[ \t]+\"(gfx|ui|audio|debug|frontend|platform)/")
        foreach(_line ${_bad})
            file(RELATIVE_PATH _rel "${CMAKE_CURRENT_SOURCE_DIR}" "${_f}")
            list(APPEND _violations "  ${_rel}: ${_line}")
        endforeach()

        # SDL_Renderer and friends never belong in core either, and an include
        # check alone would miss them arriving through SDL.h, which core does
        # legitimately use.
        file(STRINGS "${_f}" _sdlbad
             REGEX "SDL_(Renderer|Texture|Window|Surface|Event|Gamepad|AudioStream)")
        foreach(_line ${_sdlbad})
            file(RELATIVE_PATH _rel "${CMAKE_CURRENT_SOURCE_DIR}" "${_f}")
            list(APPEND _violations "  ${_rel}: ${_line}")
        endforeach()
    endforeach()

    if(_violations)
        string(REPLACE ";" "\n" _report "${_violations}")
        message(FATAL_ERROR
            "src/core/ has reached outside the simulation layer:\n"
            "${_report}\n"
            "core/ must not depend on gfx, ui, audio, debug, platform or the "
            "frontends, and must not name an SDL rendering, windowing, input "
            "or audio type. See cmake/Layering.cmake for why.")
    endif()
    message(STATUS "gigantima: src/core/ layer boundary clean")
endfunction()
