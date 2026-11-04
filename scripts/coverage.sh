#!/usr/bin/env bash

# Line and branch coverage of the library (include/ and src/) by the test
# suite, from the coverage preset's gcov counts, reported with gcovr (the
# pinned version in scripts/lib/tools.sh, or the command in GCOVR). Prints a
# per-file summary and writes an HTML report to build/coverage/report/.
#
#   CXX    the compiler, as for scripts/test.sh
#   GCOV   the gcov that matches it, when replacing g++ with gcov in its name
#          does not name it
#
# Consteval code, which is most of the reflection layer, runs inside the
# compiler, so the report measures the code that runs at run time.

set -euo pipefail

cd "$(dirname "${BASH_SOURCE[0]}")/.."
# shellcheck source=scripts/lib/tools.sh
source scripts/lib/tools.sh
resolve_compiler
resolve_tool "${GCOVR:-}" "${GCOVR_SPEC}" gcovr "gcovr ${GCOVR_SPEC##*==}"
gcovr=("${TOOL[@]}")
gcov="${GCOV:-${COMPILER/g++/gcov}}"

cmake --preset coverage -DCMAKE_CXX_COMPILER="${COMPILER}"
cmake --build --preset coverage
# gcov adds to the counts a previous run left; start from zero.
find build/coverage -name '*.gcda' -delete
ctest --preset coverage --parallel

readonly report=build/coverage/report
mkdir -p "${report}"
"${gcovr[@]}" --root . --gcov-executable "${gcov}" \
  --filter 'include/' --filter 'src/' \
  --merge-lines --exclude-unreachable-branches --exclude-throw-branches \
  --html-nested "${report}/index.html" --txt - \
  build/coverage
echo "HTML report: ${report}/index.html"
