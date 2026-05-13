# Copyright (c) Jake Helfert
# Licensed under the MIT License.
#
# libese_tracepoints.so — the LTTng UST analytic-tracing provider for
# the ESE Linux port.  libese.so does NOT hard-link lttng-ust; instead
# it dlopens this .so at startup, looks up EseTracepointGetTable, and
# populates a function-pointer table that the OSEventTrace_ dispatch
# routes through.  Boxes without lttng-ust (build-time) just don't get
# the .so, and the dispatch table stays nullptr at runtime — every
# ET* wrapper is a quiet no-op.
#
# The four source files in ${GEN_OUTPUT_DIRECTORY}/etwlttng/ are
# emitted by the etwlttng tool from EseEtwEventsPregen.txt.  See
# tools/etwlttng/.

set(ETWLTTNG_INPUT ${ESE_DEV}/src/_etw/EseEtwEventsPregen.txt)
set(ETWLTTNG_OUTPUT_DIR ${GEN_OUTPUT_DIRECTORY}/etwlttng)

set(ETWLTTNG_GENERATED
    ${ETWLTTNG_OUTPUT_DIR}/ese_tracepoints.h
    ${ETWLTTNG_OUTPUT_DIR}/ese_tracepoint_provider.cxx
    ${ETWLTTNG_OUTPUT_DIR}/ese_tracepoint_table.h
    ${ETWLTTNG_OUTPUT_DIR}/oseventtrace_dispatch.g.cxx
)

add_custom_command(
    OUTPUT ${ETWLTTNG_GENERATED}
    COMMAND ${CMAKE_COMMAND} -E make_directory ${ETWLTTNG_OUTPUT_DIR}
    COMMAND ${ETWLTTNG} ${ETWLTTNG_INPUT} ${ETWLTTNG_OUTPUT_DIR}
    DEPENDS
        ${ETWLTTNG_INPUT}
        ${ETWLTTNG}
    COMMENT "etwlttng: generating LTTng tracepoint provider + dispatch from EseEtwEventsPregen.txt"
)

add_custom_target(gen_etwlttng
    DEPENDS ${ETWLTTNG_GENERATED}
)

# Optional .so target: only built when lttng-ust is installed.
if (ESE_BUILD_TRACEPOINTS)
    add_library(ese_tracepoints SHARED
        ${ETWLTTNG_OUTPUT_DIR}/ese_tracepoint_provider.cxx
    )
    add_dependencies(ese_tracepoints gen_etwlttng)

    set_target_properties(ese_tracepoints PROPERTIES
        OUTPUT_NAME ese_tracepoints
        POSITION_INDEPENDENT_CODE ON
        C_VISIBILITY_PRESET hidden
        CXX_VISIBILITY_PRESET hidden
    )

    target_include_directories(ese_tracepoints PRIVATE
        ${ETWLTTNG_OUTPUT_DIR}
    )

    #  Provider must export EseTracepointGetTable.  Mark explicitly
    #  with __attribute__((visibility("default"))) in the generator
    #  if needed; today the table fn already has C linkage and we
    #  drop the default-hidden visibility just for that symbol via
    #  the version script below — kept simple by using -Wl,--export.
    target_link_libraries(ese_tracepoints PRIVATE PkgConfig::LTTNG_UST)
    target_link_options(ese_tracepoints PRIVATE
        -Wl,--export-dynamic-symbol=EseTracepointGetTable
    )
endif ()
