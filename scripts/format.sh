#!/usr/bin/env bash

# clang-format over the C++ sources: --check (the default) reports unformatted
# files, --fix rewrites them. Runs the pinned clang-format (scripts/lib/tools.sh),
# or the command in CLANG_FORMAT.

set -euo pipefail

cd "$(dirname "${BASH_SOURCE[0]}")/.."
# shellcheck source=scripts/lib/tools.sh
source scripts/lib/tools.sh

case "${1:---check}" in
  --check) format_args=(--dry-run --Werror) ;;
  --fix) format_args=(-i) ;;
  *)
    echo "Usage: $0 [--check|--fix]" >&2
    exit 2
    ;;
esac

resolve_tool "${CLANG_FORMAT:-}" "${CLANG_FORMAT_SPEC}" clang-format
clang_format=("${TOOL[@]}")

# New, untracked sources count as well as tracked ones; --exclude-standard skips
# build outputs, and a file deleted from the working tree is skipped too.
sources=()
while IFS= read -r -d '' source; do
  if [[ -f "${source}" ]]; then
    sources+=("${source}")
  fi
done < <(git ls-files --cached --others --exclude-standard -z -- \
  'examples/*.cpp' 'include/*.hpp' 'src/*.cpp' 'src/*.hpp' \
  'tests/*.cpp' 'tests/*.hpp')

"${clang_format[@]}" "${format_args[@]}" "${sources[@]}"
