#!/usr/bin/env bash

# Configure, build, and test one CMake preset: the tests, the compile-fail
# checks, the corpus replay, and the example.
#
#   PRESET   dev (the default), release, asan, or tsan; coverage has its own
#            script, scripts/coverage.sh
#   CXX      the compiler, when the detected GCC 16.2 (scripts/lib/tools.sh) is
#            not the one to use
#   FRESH=1  reconfigure the preset's build directory from scratch
#
# Extra arguments go to ctest, for example `-L json` or `-R 'variant'`.

set -euo pipefail

cd "$(dirname "${BASH_SOURCE[0]}")/.."
# shellcheck source=scripts/lib/tools.sh
source scripts/lib/tools.sh
preset="${PRESET:-dev}"
resolve_compiler

configure_args=(--preset "${preset}" -DCMAKE_CXX_COMPILER="${COMPILER}")
if [[ "${FRESH:-0}" == 1 ]]; then
  configure_args+=(--fresh)
fi
cmake "${configure_args[@]}"
cmake --build --preset "${preset}"
ctest --preset "${preset}" --parallel "$@"
