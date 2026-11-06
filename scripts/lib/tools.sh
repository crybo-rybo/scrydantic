# shellcheck shell=bash
# The version constants are read by the scripts that source this file.
# shellcheck disable=SC2034

# Sourced by the scripts in scripts/: the pinned versions of the developer tools
# that come from PyPI, and how to run them. CI runs these same versions, so a
# check that passes locally passes there.
#
# A tool runs, in order of preference, as
#   1. the command in its override variable (CLANG_FORMAT, CLANG_TIDY, LIZARD,
#      GCOVR, or SHELLCHECK), which may include arguments;
#   2. the command on PATH, when it reports exactly the pinned version;
#   3. uvx (https://docs.astral.sh/uv/), or pipx run, at the pinned version.

readonly CLANG_FORMAT_SPEC="clang-format==22.1.8"
readonly CLANG_TIDY_SPEC="clang-tidy==22.1.8"
readonly LIZARD_SPEC="lizard==1.25.0"
readonly GCOVR_SPEC="gcovr==8.8"
readonly SHELLCHECK_SPEC="shellcheck-py==0.11.0.1"

# resolve_tool <override> <package==version> <command> [<version>]
#
# Sets the array TOOL to the words that run <command>. <version> is the text the
# command's --version output must contain for the copy on PATH to count; it
# defaults to the version in the package spec.
resolve_tool() {
  local override="$1" spec="$2" command="$3"
  local version="${4:-${spec##*==}}"
  if [[ -n "${override}" ]]; then
    read -r -a TOOL <<<"${override}"
  elif command -v "${command}" >/dev/null 2>&1 &&
    "${command}" --version 2>/dev/null | grep -qF -- "${version}"; then
    TOOL=("${command}")
  elif command -v uvx >/dev/null 2>&1; then
    TOOL=(uvx --quiet --from "${spec}" "${command}")
  elif command -v pipx >/dev/null 2>&1; then
    TOOL=(pipx run --spec "${spec}" "${command}")
  else
    echo "error: ${command} ${version} is not on PATH, and neither uvx nor pipx" \
      "is available to run ${spec}; install uv (https://docs.astral.sh/uv/)" >&2
    exit 1
  fi
}

# The oldest GCC that CMakeLists.txt accepts: the first GCC 16 release. GCC
# 16.0.x versions are development snapshots.
readonly MINIMUM_GCC_VERSION=16.1

# is_supported_gcc <command>
#
# Succeeds when <command> runs and is GCC MINIMUM_GCC_VERSION or newer.
is_supported_gcc() {
  local version
  version="$("$1" -dumpfullversion 2>/dev/null)" || return 1
  [[ "$(printf '%s\n' "${MINIMUM_GCC_VERSION}" "${version}" | sort -V | head -n 1)" == \
    "${MINIMUM_GCC_VERSION}" ]]
}

# resolve_compiler
#
# Sets COMPILER to the C++ compiler to configure with: CXX when it is set, else
# g++ or g++-16, whichever is first to be GCC 16.1 or newer.
resolve_compiler() {
  if [[ -n "${CXX:-}" ]]; then
    COMPILER="${CXX}"
  elif is_supported_gcc g++; then
    COMPILER=g++
  elif is_supported_gcc g++-16; then
    COMPILER=g++-16
  else
    echo "error: found no GCC ${MINIMUM_GCC_VERSION} or newer as g++ or" \
      "g++-16; set CXX to one" >&2
    exit 1
  fi
}
