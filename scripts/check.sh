#!/usr/bin/env bash

# Everything CI checks, run locally before a push: formatting, the static checks
# (clang-tidy, include boundaries, hygiene, shellcheck, complexity), and the
# tests of the dev preset.
#
#   scripts/check.sh         formatting, static checks, and the dev tests
#   scripts/check.sh --all   the same, then the release, asan, and tsan tests
#
# CI also installs the package and builds a consumer of it; see
# .github/workflows/ci.yml.

set -euo pipefail

cd "$(dirname "${BASH_SOURCE[0]}")/.."
case "${1:-}" in
  "") presets=(dev) ;;
  --all) presets=(dev release asan tsan) ;;
  *)
    echo "Usage: $0 [--all]" >&2
    exit 2
    ;;
esac

step() { printf '\n==> %s\n' "$*"; }

step "Formatting"
scripts/format.sh --check
step "Static checks"
scripts/lint.sh
for preset in "${presets[@]}"; do
  step "Tests (${preset})"
  PRESET="${preset}" scripts/test.sh
done
step "All checks passed"
