#!/usr/bin/env bash
# Copyright (c) Jake Helfert
# Licensed under the MIT License.
#
# Run every ESE test binary in a fresh scratch tree and report a
# summary.  Each suite gets its own subdirectory under the current
# working directory; everything the engine writes (.edb / .log / .chk
# / .jfm / assert.txt / per-scenario scratch) lands inside that
# subdir, so cleanup is `rm -rf test-output` (or whatever working
# directory you invoked from).
#
# Usage:
#   ./run-all-tests.sh [<build-root>]
#
# <build-root> is the CMake build directory containing bin/ — for
# example `./build` (the default if you omit the argument) or
# `./build-release`.  The script doesn't care which configuration
# you point it at.

set -u

build_root="${1:-./build}"
build_root="$(cd "${build_root}" 2>/dev/null && pwd)" || {
    echo "error: build root '${1:-./build}' not found" >&2
    exit 2
}
bin_dir="${build_root}/bin"
if [[ ! -d "${bin_dir}" ]]; then
    echo "error: ${bin_dir} not found — is this a CMake build directory?" >&2
    exit 2
fi

# Each suite runs from its own subdir under CWD.  Caller picks CWD by
# `cd`'ing somewhere safe before invocation; the CMake target below
# routes that to <build-root>/test-output.
root="$(pwd)"

if [[ -t 1 ]]; then
    c_pass=$'\033[1;32m'   c_fail=$'\033[1;31m'   c_skip=$'\033[1;33m'
    c_head=$'\033[1;36m'   c_off=$'\033[0m'
else
    c_pass=""              c_fail=""              c_skip=""
    c_head=""              c_off=""
fi

declare -A suite_status   # name -> "pass" / "fail" / "skip"
declare -a suite_order    # preserves insertion order for the summary

run_suite() {
    # run_suite <name> <binary-name> [<args>...]
    local name="$1"; shift
    local exe="$1"; shift
    local args=( "$@" )

    suite_order+=( "${name}" )

    if [[ ! -x "${bin_dir}/${exe}" ]]; then
        echo "${c_skip}[ SKIP ] ${name}${c_off} — ${bin_dir}/${exe} not built"
        suite_status[${name}]="skip"
        return
    fi

    local suite_dir="${root}/${name}"
    rm -rf "${suite_dir}"
    mkdir -p "${suite_dir}"

    echo "${c_head}===== ${name} =====${c_off}"
    local start=$SECONDS
    if ( cd "${suite_dir}" && "${bin_dir}/${exe}" "${args[@]}" ) > "${suite_dir}/output.log" 2>&1; then
        local elapsed=$(( SECONDS - start ))
        echo "${c_pass}[ PASS ] ${name}${c_off} (${elapsed}s)"
        suite_status[${name}]="pass"
    else
        local rc=$?
        local elapsed=$(( SECONDS - start ))
        echo "${c_fail}[ FAIL ] ${name}${c_off} (rc=${rc}, ${elapsed}s)"
        echo "         log: ${suite_dir}/output.log"
        tail -n 20 "${suite_dir}/output.log" | sed 's/^/         /'
        suite_status[${name}]="fail"
    fi
}

# Order: cheap stuff first so fast failures surface early.
run_suite "ese-config-test"    ese-config-test
run_suite "devlibtest-collection" COLLECTIONUNIT
run_suite "devlibtest-errvalidator" ERRVALIDATOR
run_suite "devlibtest-cclayer"  CcLayerUnit
run_suite "devlibtest-iterquery" IterQueryUnit
run_suite "devlibtest-stat"     STATUNIT
run_suite "devlibtest-resmgr"   RESMGRUNIT
run_suite "devlibtest-sync"     SYNCUNIT
run_suite "nls-smoke"           nls_smoke
run_suite "tier1"               EseLibWithTestsRunner
run_suite "tier2"               EseLibWithTestsRunner -d .
run_suite "ese-tests"           ese-tests

# ----- summary -----
echo
echo "${c_head}===== summary =====${c_off}"
pass_count=0
fail_count=0
skip_count=0
for name in "${suite_order[@]}"; do
    case "${suite_status[${name}]}" in
        pass) printf "  ${c_pass}pass${c_off}  %s\n" "${name}"; ((pass_count++)) ;;
        fail) printf "  ${c_fail}FAIL${c_off}  %s\n" "${name}"; ((fail_count++)) ;;
        skip) printf "  ${c_skip}skip${c_off}  %s\n" "${name}"; ((skip_count++)) ;;
    esac
done
echo
echo "${pass_count} passed, ${fail_count} failed, ${skip_count} skipped"

if (( fail_count > 0 )); then
    exit 1
fi
exit 0
