# Copyright (c) Microsoft Corporation.
# Licensed under the MIT License.

add_custom_command(
    OUTPUT ${GEN_OUTPUT_DIRECTORY}/_res/jetmsgex.mc
    COMMAND ${CMAKE_COMMAND} -E make_directory ${GEN_OUTPUT_DIRECTORY}/_res
    COMMAND perl ${CMAKE_CURRENT_SOURCE_DIR}/fixmc.pl ${CMAKE_CURRENT_SOURCE_DIR}/jetmsg.mc ${GEN_OUTPUT_DIRECTORY}/_res/jetmsgex.mc
    DEPENDS
        fixmc.pl
        jetmsg.mc
)

set(MC_INPUTS
    ${GEN_OUTPUT_DIRECTORY}/_res/jetmsgex.mc
)

#  Linux uses our mc replacement (tools/mc), which gains a -c flag to also
#  emit a C++ message-table source.  Windows uses the SDK mc.exe, which
#  doesn't understand -c; the .res path it produces is what FormatMessage
#  consumes there.
if (WIN32)
    set(MC_EXTRA_FLAGS "")
else ()
    set(MC_EXTRA_FLAGS -c ${GEN_OUTPUT_DIRECTORY}/_res)
endif ()

foreach (MC_INPUT ${MC_INPUTS})
    get_filename_component(MC_OUTPUT ${MC_INPUT} NAME_WLE)

    string(REPLACE ".mc" "" MC_OUTPUT ${MC_OUTPUT})

    set(MC_OUTPUTS
        ${GEN_OUTPUT_DIRECTORY}/_res/${MC_OUTPUT}.h
        ${GEN_OUTPUT_DIRECTORY}/_res/${MC_OUTPUT}.rc
    )
    if (NOT WIN32)
        list(APPEND MC_OUTPUTS ${GEN_OUTPUT_DIRECTORY}/_res/${MC_OUTPUT}_table.cxx)
    endif ()

    add_custom_command(
        OUTPUT ${MC_OUTPUTS}
        COMMAND ${CMAKE_MESSAGE_COMPILER}
            -h ${GEN_OUTPUT_DIRECTORY}/_res
            -r ${GEN_OUTPUT_DIRECTORY}/_res
            ${MC_EXTRA_FLAGS}
            -v ${MC_INPUT}
        DEPENDS ${MC_INPUT}
    )

    add_custom_target(gen_${MC_OUTPUT}
        DEPENDS
            ${MC_INPUT}
            ${MC_OUTPUTS}
    )
endforeach()
