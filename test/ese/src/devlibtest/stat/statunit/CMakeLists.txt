# Copyright (c) Microsoft Corporation.
# Licensed under the MIT License.

add_executable(STATUNIT
    util.cxx
    main.cxx
    histoutil.cxx
    CompoundHistoSuite.cxx
    FixedLinearHistoSuite.cxx
    FunctionalStatSuite.cxx
    statbasic.cxx
    segmentedhisto.cxx
    linearhisto.cxx
    movingavg.cxx
)

target_compile_definitions(STATUNIT PRIVATE
    _MBCS
    BUILD_ENV_IS_EX
    ESE_FLAVOUR_IS_ESE
)

target_include_directories(STATUNIT PRIVATE
    ${ESE_DEV}/published/inc/
    ${INC_OUTPUT_DIRECTORY}
    ${ESE_TEST}/src/inc
)

target_link_libraries(STATUNIT PRIVATE
    BstfUnitTest
    sync
)

if (NOT WIN32)
    target_link_options(STATUNIT PRIVATE -Wl,-z,muldefs)
endif ()