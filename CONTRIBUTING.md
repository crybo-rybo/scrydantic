# Contributing

This file covers the development workflow and the checks that a change must
pass. The library's design is described in
[docs/architecture.md](docs/architecture.md).

## Toolchain

- GCC 16 or newer, the only compiler that implements C++26 reflection. The
  scripts use `CXX` when it is set. Otherwise they use `g++` if it is GCC 16
  (Arch, Fedora), and then `g++-16` (Ubuntu with the toolchain PPA).
- CMake 3.30 or newer, and Ninja.
- [uv](https://docs.astral.sh/uv/) or pipx. The scripts use them to run the
  pinned clang-format, clang-tidy, lizard, gcovr, and shellcheck from PyPI.
  The pinned versions are in `scripts/lib/tools.sh`. If a tool on `PATH`
  reports exactly the pinned version, the scripts use it. To use a different
  command, set `CLANG_FORMAT`, `CLANG_TIDY`, `LIZARD`, `GCOVR`, or `SHELLCHECK`.

You do not need a clang compiler. clang-tidy checks the C++23 code, and GCC's
warnings check the reflection code.

## Everyday commands

| Command | What it does |
|---|---|
| `scripts/check.sh` | Runs everything that CI checks on a change: formatting, static checks, and the dev tests. Run it before you push. `--all` also runs the release, asan, and tsan tests. |
| `scripts/test.sh` | Configures, builds, and tests one preset. Set `PRESET` to `dev` (the default), `release`, `asan`, or `tsan`. Extra arguments go to ctest, for example `-L json`. Set `FRESH=1` to configure from scratch. |
| `scripts/format.sh --fix` | Formats the C++ sources. `--check` (the default) only reports unformatted files. |
| `scripts/lint.sh` | Runs clang-tidy, the include-boundary and hygiene checks, shellcheck, and the complexity check. |
| `scripts/complexity.sh --report` | Lists the most complex functions and the totals. |
| `scripts/coverage.sh` | Reports line coverage of `include/` and `src/` by the tests, and writes `build/coverage/report/index.html`. |
| `scripts/release-notes.sh vX.Y.Z` | Checks a release tag against `version.hpp` and `CHANGELOG.md`, and prints the release notes. |
| `cmake --workflow --preset <name>` | Configures, builds, and tests a preset, without a script. |

To run the quick checks (formatting and complexity) before each commit, enable
the repository's hook once for each clone:

```sh
git config core.hooksPath .githooks
```

## Presets

`CMakePresets.json` defines configure, build, test, and workflow presets. Each
preset builds in `build/<name>`.

| Preset | Build |
|---|---|
| `dev` | Debug, with libstdc++ assertions (`_GLIBCXX_ASSERTIONS`) |
| `release` | RelWithDebInfo, with the benchmarks |
| `asan` | `dev` with AddressSanitizer and UndefinedBehaviorSanitizer |
| `tsan` | `dev` with ThreadSanitizer |
| `coverage` | `dev` with gcov instrumentation, used by `scripts/coverage.sh` |

`compile_commands.json` is exported for each preset. `.clangd` points at the
dev build. Clang cannot parse reflection yet, so clangd shows errors in the
C++26 headers and their tests. These errors do not mean that the build is
broken.

## What the checks enforce

- **Warnings.** Scrydantic's own targets compile with the warnings in
  `cmake/ScrydanticProjectOptions.cmake`, and with `-Werror` when Scrydantic is
  the top-level project. These flags are never exported to consumers.
- **clang-tidy** (`.clang-tidy`) runs on the C++23 code: the JSON core and the
  fuzz harness. It enables whole families of checks, and each exclusion has a
  documented reason. A function's cognitive complexity must not exceed 25.
- **Complexity** (`scripts/complexity.sh`) applies to all C++ code, including
  the reflection code. A function must have a cyclomatic complexity of at most
  15, at most 80 lines, and at most 6 parameters. If a function exceeds a limit,
  split it. Do not raise the limit.
- **Boundaries.** The library depends only on the standard library. The
  allowed include lists for headers, the core, examples, and tests are in
  `scripts/lint.sh` and in [docs/architecture.md](docs/architecture.md#boundaries).
- **Hygiene.** No line ends with whitespace, every file ends with a newline,
  and every `TODO` links an issue. The fuzz corpus and the golden archive are
  byte-exact and are exempt from these checks.

## Tests

The tests are under `tests/`. Each test has a CTest label, so
`ctest --preset dev -L <label>` runs one group:

| Label | Tests |
|---|---|
| `json` | The C++23 core: the document, `JsonView`, the text operations, and the golden fixtures (`tests/json/`), and the version macros (`tests/version/`) |
| `reflection` | The codec, the schemas, and tool binding (`tests/codec/`, `tests/tools/`) |
| `fuzz` | The fuzz corpus replayed through the property target (`tests/fuzz/`) |
| `multi-tu` | Call-site keying across translation units (`tests/multi_tu/`) |
| `compile-fail` | Programs that must not compile, each with the diagnostic that it must print (`tests/compile_fail/`) |
| `example` | `examples/mcp_tools.cpp`, which exits 0 only when its output is the expected output |

Every bug fix needs a regression test. Some changes need more than a Catch2
case:

- **A new rejection** needs a `scrydantic_add_compile_fail_test()` in
  `tests/CMakeLists.txt`, with a substring of the diagnostic that names the
  type, member, or function and the reason. Also add `static_assert`s on the
  text that the matching function in `diagnostics.hpp` gives for the
  rejection, as `tests/codec/codec_tests.cpp` does.
- **A change to what the parser accepts or writes** needs an update to the
  golden fixtures. See [tests/fixtures/json/README.md](tests/fixtures/json/README.md)
  for how to update them.
- **A new fuzz seed** goes under `tests/fuzz/corpus/json/`. The prefix test
  also needs a line in `corpus_prefixes.list` for the seed. The fixtures
  README explains this.
- **A change to the encoder** must keep the differential spelling tests in
  `tests/codec/codec_tests.cpp` passing. Those tests check that `encode()`
  writes exactly what canonicalizing the verbatim writer's text writes.

CI's `package` jobs test the installed package and the source tree as a
subproject. You can also test them manually: configure `tests/package/` with
`CMAKE_PREFIX_PATH` set to an installation prefix, or with
`SCRYDANTIC_SOURCE_DIR` set to the repository root.

## Benchmarks

```sh
cmake --workflow --preset release
build/release/tests/scrydantic_benchmarks             # every benchmark
build/release/tests/scrydantic_benchmarks '[codec]'   # or [json], [tools]
```

Compare a hot-path change with the numbers from before the change, on the same
machine. Benchmarks run only from an optimized build, so they are not
registered with CTest. [docs/architecture.md](docs/architecture.md#performance)
records the current baseline.

## Changelog

A change that a user can see (the public API, the behavior, the supported
types, the diagnostics, or the build and the package) adds an entry under
`## [Unreleased]` in [CHANGELOG.md](CHANGELOG.md). Use the
[Keep a Changelog](https://keepachangelog.com/en/1.1.0/) sections: `Added`,
`Changed`, `Deprecated`, `Removed`, `Fixed`, and `Security`. Write the entries
in ASD-STE100 Simplified Technical English: short sentences, active voice, and
one meaning for each word.

## Releases

The version is set in one place, `include/scrydantic/version.hpp`. CMake reads
it from there. Scrydantic follows Semantic Versioning. Before 1.0, increase the
minor version for a change that breaks the public API, and the patch version
for all other changes.

1. On a branch, set the new version in `include/scrydantic/version.hpp`.
2. In `CHANGELOG.md`, rename `## [Unreleased]` to `## [X.Y.Z] - YYYY-MM-DD`,
   add a new empty `## [Unreleased]` above it, and update the compare links at
   the end of the file.
3. Update the version in the README's `FetchContent` example, and the
   supported versions in `SECURITY.md` for a new minor series.
4. Run `scripts/release-notes.sh vX.Y.Z` and `scripts/check.sh --all`, then
   merge the pull request.
5. Tag the merge commit and push the tag:

   ```sh
   git switch main && git pull --ff-only
   git tag -a vX.Y.Z -m "Scrydantic vX.Y.Z"
   git push origin vX.Y.Z
   ```

The tag starts `.github/workflows/release.yml`. It checks the tag with
`scripts/release-notes.sh` and runs every CI job on the tagged commit. Then it
publishes a GitHub release with the changelog section as the notes, a source
archive (`scrydantic-X.Y.Z.tar.gz`), its SHA-256 checksum, and a build
provenance attestation. If a job fails, no release is published. Fix the
cause on `main`, delete the tag, and tag again.

## Conventions

- **Failures are values.** A fallible operation returns `Result` or `Status`.
  Exceptions are used only for allocation failure.
- **Diagnostics name the path and the reason.** A rejected type fails with one
  `static_assert` that names the member path (`Outer::nested.letters[]`) and
  says why. A run-time failure names the JSON path (`$.tools[0].name`). Put a
  message in `model_message` only if every word of it comes from text that the
  model already has.
- **Aggregate members have `{}` defaults**, so that partial designated
  initialization does not trigger warnings.
- **Comments explain why.** A comment explains why the code is written as it
  is. In particular, add a comment where a GCC 16 limitation shapes the code,
  and add the limitation to the list in the architecture document.
- **`detail` is internal.** Change anything under `detail/` freely. A change
  to the public API, which is everything else
  ([docs/architecture.md](docs/architecture.md#api-stability)), also updates
  the README.
- **No new dependencies** for the library. Tests can use Catch2.
