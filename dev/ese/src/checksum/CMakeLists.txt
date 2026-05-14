# Copyright (c) Microsoft Corporation.
# Licensed under the MIT License.

add_subdirectory(avx)
add_subdirectory(main)

#  On non-x86 targets the avx subdir creates an INTERFACE stub
#  for `checksum_avx` so the rest of the build can keep
#  `target_link_libraries(... checksum_avx)` unconditionally.