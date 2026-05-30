# Copyright (c) Microsoft Corporation.
# Licensed under the MIT License.

# This order is not the actual order of building of subdirectories
# (which is based on dependency order). Therefore, it's better to keep to
# the alphabetized order.
add_subdirectory(_errstr)
add_subdirectory(_esefile)
add_subdirectory(_etw)
add_subdirectory(_lz4)
add_subdirectory(_perfctrs)
add_subdirectory(_res)
add_subdirectory(_xpress)
add_subdirectory(checksum)
add_subdirectory(dht)
# embeddedunittest links kernel32/advapi32/eselibwithtests/oslite — all
# Windows-only — and uses GetProcessTimes etc. that aren't shimmed.
if (WIN32)
    add_subdirectory(embeddedunittest)
endif ()
add_subdirectory(ese)
add_subdirectory(eseutil)
add_subdirectory(os)
add_subdirectory(sync)