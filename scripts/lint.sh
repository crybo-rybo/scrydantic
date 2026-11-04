#!/usr/bin/env bash

# The static checks CI runs, beyond formatting (scripts/format.sh):
#
#   - clang-tidy (.clang-tidy) over the C++23 code: the JSON core under
#     src/json/ and the fuzz harness. The rest of Scrydantic is C++26
#     reflection, which no clang release can parse yet; GCC's warnings
#     (cmake/ScrydanticProjectOptions.cmake) cover it instead.
#   - the include boundaries the library promises, and repository hygiene;
#   - shellcheck over the scripts;
#   - scripts/complexity.sh.
#
# Every check runs even after one fails, and the script exits nonzero if any
# did. Tools are the pinned ones in scripts/lib/tools.sh, or the commands in
# CLANG_TIDY and SHELLCHECK.

set -euo pipefail

cd "$(dirname "${BASH_SOURCE[0]}")/.."
# shellcheck source=scripts/lib/tools.sh
source scripts/lib/tools.sh
status=0

fail() {
  echo "error: $*" >&2
  status=1
}

# ---- clang-tidy -------------------------------------------------------------

resolve_tool "${CLANG_TIDY:-}" "${CLANG_TIDY_SPEC}" clang-tidy \
  "version ${CLANG_TIDY_SPEC##*==}"
clang_tidy=("${TOOL[@]}")
cpp23_sources=()
while IFS= read -r source; do cpp23_sources+=("${source}"); done \
  < <(git ls-files --cached --others --exclude-standard -- 'src/json/*.cpp' 'tests/fuzz/*.cpp')
"${clang_tidy[@]}" --quiet "${cpp23_sources[@]}" -- -std=c++23 -Iinclude -Isrc ||
  fail "clang-tidy reported the findings above"

# ---- Include boundaries -----------------------------------------------------

# A standard library header is a bare lowercase name, such as <meta>.
readonly standard_header='<[a-z_0-9]+>'

# The library depends on the standard library alone: a public header includes
# only standard headers and other Scrydantic headers.
if git grep --untracked -nE '#[[:space:]]*include' -- 'include/*.hpp' |
  grep -vE "#[[:space:]]*include[[:space:]]*(${standard_header}|<scrydantic/[a-z_/]+\.hpp>)"; then
  fail "a public header includes something other than the standard library or" \
    "a Scrydantic header"
fi

# The JSON core may include only the standard library, its own headers, and
# the three public headers that are C++23 like it (src/json/core.hpp).
if git grep --untracked -nE '#[[:space:]]*include' -- 'src/*.cpp' 'src/*.hpp' |
  grep -vE "#[[:space:]]*include[[:space:]]*(${standard_header}|\"json/[a-z_]+\.hpp\"|<scrydantic/(error|json|detail/json_text)\.hpp>)"; then
  fail "the JSON core includes a header outside its boundary"
fi

# Examples use the library as a consumer does; tests may add Catch2 and local
# or core headers ("...").
if git grep --untracked -nE '#[[:space:]]*include' -- 'examples/*.cpp' |
  grep -vE "#[[:space:]]*include[[:space:]]*(${standard_header}|<scrydantic/[a-z_/]+\.hpp>)"; then
  fail "an example includes something a consumer of the package could not"
fi
if git grep --untracked -nE '#[[:space:]]*include' -- 'tests/*.cpp' 'tests/*.hpp' |
  grep -vE "#[[:space:]]*include[[:space:]]*(${standard_header}|<scrydantic/[a-z_/]+\.hpp>|<catch2/[a-z_/]+\.hpp>|\"[a-z_/]+\.hpp\")"; then
  fail "a test includes something other than the standard library, Scrydantic," \
    "Catch2, or a local header"
fi

# ---- Hygiene ----------------------------------------------------------------

if git grep --untracked -nE '//[[:space:]]*TODO([^[:alnum:]_]|$)' -- include src examples tests |
  grep -vE 'https?://|#[0-9]+'; then
  fail "a TODO comment must link an issue"
fi

# The fuzz corpus and the golden archive are byte-exact (.gitattributes), so
# they are exempt.
readonly exempt=(':!tests/fuzz/corpus/' ':!tests/fixtures/json/*.tar.xz')
if git grep --untracked -nIE '[[:space:]]+$' -- . "${exempt[@]}"; then
  fail "the lines above end in whitespace"
fi
while IFS= read -r -d '' file; do
  if [[ -s "${file}" && "$(tail -c 1 "${file}" | od -An -c | tr -d ' ')" != '\n' ]]; then
    fail "${file} does not end with a newline"
  fi
done < <(git grep --untracked -lIz '' -- . "${exempt[@]}")

# ---- Shell scripts ----------------------------------------------------------

resolve_tool "${SHELLCHECK:-}" "${SHELLCHECK_SPEC}" shellcheck "version: 0.11.0"
shellcheck=("${TOOL[@]}")
shell_scripts=()
while IFS= read -r script; do shell_scripts+=("${script}"); done \
  < <(git ls-files --cached --others --exclude-standard -- 'scripts/*.sh' '.githooks/*')
"${shellcheck[@]}" --external-sources "${shell_scripts[@]}" ||
  fail "shellcheck reported the findings above"

# ---- Complexity -------------------------------------------------------------

scripts/complexity.sh || fail "scripts/complexity.sh reported the functions above"

exit "${status}"
