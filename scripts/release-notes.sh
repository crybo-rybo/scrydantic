#!/usr/bin/env bash

# Check that a release tag agrees with the repository, then print the release
# notes for it: the tag's section of CHANGELOG.md, without its heading.
#
#   scripts/release-notes.sh v0.1.0
#
# The script fails when the tag is not vMAJOR.MINOR.PATCH, when
# include/scrydantic/version.hpp sets a different version, or when
# CHANGELOG.md has no dated section for the version, or an empty one.
# The release workflow (.github/workflows/release.yml) runs it on each tag.

set -euo pipefail

cd "$(dirname "${BASH_SOURCE[0]}")/.."

if [[ $# -ne 1 || ! "$1" =~ ^v([0-9]+)\.([0-9]+)\.([0-9]+)$ ]]; then
  echo "Usage: $0 vMAJOR.MINOR.PATCH" >&2
  exit 2
fi
version="${1#v}"

header_component() {
  sed -nE "s/^#define SCRYDANTIC_VERSION_$1 ([0-9]+)$/\1/p" include/scrydantic/version.hpp
}
header_version="$(header_component MAJOR).$(header_component MINOR).$(header_component PATCH)"
if [[ "${header_version}" != "${version}" ]]; then
  echo "error: the tag is $1, but include/scrydantic/version.hpp sets ${header_version}" >&2
  exit 1
fi

# The section starts at "## [X.Y.Z] - YYYY-MM-DD" and ends at the next "## "
# heading or at the link definitions at the end of the file.
heading="^## \[${version//./\\.}\] - [0-9]{4}-[0-9]{2}-[0-9]{2}$"
if ! grep -qE "${heading}" CHANGELOG.md; then
  echo "error: CHANGELOG.md has no section \"## [${version}] - YYYY-MM-DD\"" >&2
  exit 1
fi
# The pattern goes through the environment, because awk -v would process its
# backslashes as escape sequences.
notes="$(HEADING="${heading}" awk '
  $0 ~ ENVIRON["HEADING"] { inside = 1; next }
  inside && (/^## / || /^\[[^]]+\]: /) { exit }
  inside { print }
' CHANGELOG.md | sed -e '/./,$!d')"
if [[ -z "${notes//[[:space:]]/}" ]]; then
  echo "error: the CHANGELOG.md section for ${version} is empty" >&2
  exit 1
fi
printf '%s\n' "${notes}"
