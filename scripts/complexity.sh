#!/usr/bin/env bash

# Function complexity over every C++ source, reflection code included, with
# lizard (https://github.com/terryyin/lizard), the pinned version in
# scripts/lib/tools.sh or the command in LIZARD.
#
#   scripts/complexity.sh            fail on any function past a threshold
#   scripts/complexity.sh --report   the most complex functions and the totals
#
# lizard counts decisions from tokens, so it reads `^^`, splices, and
# `template for` like any other code; an `if constexpr` branch counts as a
# decision, as it does for a reader. clang-tidy's cognitive-complexity check
# (.clang-tidy) applies on top of this to the C++23 code.

set -euo pipefail

cd "$(dirname "${BASH_SOURCE[0]}")/.."
# shellcheck source=scripts/lib/tools.sh
source scripts/lib/tools.sh
resolve_tool "${LIZARD:-}" "${LIZARD_SPEC}" lizard
lizard=("${TOOL[@]}")

# A function past any of these fails the check. 15 is lizard's own default
# cyclomatic complexity limit; split a function rather than raise it.
readonly max_ccn=15
readonly max_lines=80
readonly max_parameters=6
readonly sources=(include src examples tests)

case "${1:-}" in
  "")
    if ! "${lizard[@]}" --languages cpp --warnings_only --CCN "${max_ccn}" \
      --length "${max_lines}" --arguments "${max_parameters}" "${sources[@]}"; then
      echo "error: the functions above exceed a complexity threshold" \
        "(CCN ${max_ccn}, ${max_lines} lines, ${max_parameters} parameters)" >&2
      exit 1
    fi
    ;;
  --report)
    echo "The 20 functions with the highest cyclomatic complexity (CCN):"
    "${lizard[@]}" --languages cpp --warnings_only --CCN 0 \
      --sort cyclomatic_complexity "${sources[@]}" | head -n 20 || true
    echo
    "${lizard[@]}" --languages cpp "${sources[@]}" | tail -n 4
    ;;
  *)
    echo "Usage: $0 [--report]" >&2
    exit 2
    ;;
esac
