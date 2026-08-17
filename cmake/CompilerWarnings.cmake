# CompilerWarnings.cmake - the warning set, applied to first-party targets only.
#
# Vendored code in ext/ is deliberately untouched: SDL builds clean under its
# own settings and would not survive -Wconversion, and warnings we cannot fix
# are warnings people learn to scroll past.

function(gg_configure target)
    if(NOT GG_HAVE_NULLPTR)
        target_compile_definitions(${target} PRIVATE GG_NO_NULLPTR)
    endif()
    target_compile_options(${target} PRIVATE ${GG_C_FLAGS})

    if(MSVC)
        target_compile_options(${target} PRIVATE /W4 /utf-8)
        if(GIGANTIMA_WERROR)
            target_compile_options(${target} PRIVATE /WX)
        endif()
    else()
        target_compile_options(${target} PRIVATE
            -Wall -Wextra -Wshadow -Wconversion -Wsign-conversion
            -Wpointer-arith -Wstrict-prototypes -Wmissing-prototypes
            -Wno-unused-parameter)
        if(GIGANTIMA_WERROR)
            target_compile_options(${target} PRIVATE -Werror)
        endif()
    endif()

    if(GIGANTIMA_ASAN AND NOT MSVC)
        target_compile_options(${target} PRIVATE
            -fsanitize=address,undefined -fno-omit-frame-pointer)
        target_link_options(${target} PRIVATE -fsanitize=address,undefined)
    endif()
endfunction()
