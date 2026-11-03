#!/usr/bin/env bash

# clang-tidy over the C++23 JSON core (the rest of Scrydantic is C++26
# reflection, which clang-tidy cannot parse), plus the repository rules
# clang-tidy does not cover. Uses the clang-tidy on PATH, or CLANG_TIDY. CI uses
# clang-tidy 22.

set -euo pipefail

cd "$(dirname "${BASH_SOURCE[0]}")/.."
clang_tidy="${CLANG_TIDY:-clang-tidy}"
status=0

json_core_sources=()
while IFS= read -r source; do json_core_sources+=("${source}"); done \
  < <(git ls-files 'src/json/*.cpp')
"${clang_tidy}" --quiet "${json_core_sources[@]}" -- -std=c++23 \
  -Iinclude -Isrc || status=1

# The JSON core may include only its own public JSON headers and other headers
# of the core.
if git grep -nE '#[[:space:]]*include[[:space:]]*[<"][a-z_]+/' -- 'src/json/' |
  grep -vE '"json/[a-z_]+\.hpp"' |
  grep -vE '<scrydantic/(error|json|detail/json_text)\.hpp>'; then
  echo "error: the JSON core includes a header outside its boundary" >&2
  status=1
fi

# Scrydantic is self-contained: nothing includes a Scry header, a header from
# Scry's src/, or libcurl, and nothing links libcurl or Threads.
if git grep -nE '#[[:space:]]*include[[:space:]]*(<(scry|curl)/|"(core|kernel|machine|provider|reflection|runtime)/)' \
  -- '*.cpp' '*.hpp'; then
  echo "error: Scrydantic includes a header outside its boundary" >&2
  status=1
fi
if git grep -nE '(CURL|Threads)::' -- '*CMakeLists.txt' '*.cmake' '*.cmake.in'; then
  echo "error: Scrydantic links a dependency outside its boundary" >&2
  status=1
fi

if git grep -nE '//[[:space:]]*TODO([^[:alnum:]_]|$)' -- include src examples tests |
  grep -vE 'https?://|#[0-9]+'; then
  echo "error: TODO comments must link an issue" >&2
  status=1
fi

exit "${status}"
