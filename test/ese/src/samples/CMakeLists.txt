# Copyright (c) Microsoft Corporation.
# Licensed under the MIT License.

# BookStoreSample and SvcInitSample link libese.so and use Win32-shim
# helpers (e.g. GetLocalTime) that libese.so no longer re-exports now that
# its version script exports only the public Jet* API.  Dropped from the
# build; the integration-tests suite is the supported API exerciser.
#add_subdirectory(BookStoreSample)
#add_subdirectory(SvcInitSample)