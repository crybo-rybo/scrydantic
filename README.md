# Scrydantic

Scrydantic uses C++26 static reflection to generate JSON Schema, strictly
encode and decode C++ values, and bind annotated C++ functions as tools that a
model or any other JSON client can call. Declare plain structs and functions,
annotate them, and get a schema, a strict decoder with path-based errors, a
canonical encoder, and a type-erased handler, with no code generation step and
no dependency beyond the standard library.

## Build and test

Use GCC 16 or newer with reflection support, CMake 3.30 or newer, and Ninja.
From the repository root:

```sh
CXX=g++-16 cmake --workflow --preset dev    # configure, build, and run every test
build/dev/examples/scrydantic_mcp_tools_example
cmake --install build/dev --prefix "$PWD/stage"
```

`CXX` names the compiler on the first configure. Omit it where `g++` is
already GCC 16. Tests fetch Catch2; the library itself depends on nothing but
the standard library.

The scripts that CI runs are the same ones to use locally, and
`scripts/check.sh` runs all of them:

```sh
scripts/check.sh                # formatting, static checks, and the dev tests
PRESET=asan scripts/test.sh     # also dev, release, and tsan
scripts/format.sh --fix         # clang-format, pinned to CI's version
scripts/coverage.sh             # line coverage of the library by its tests
```

[CONTRIBUTING.md](CONTRIBUTING.md) describes the presets, every check and its
thresholds, the test layout, and the benchmarks.

## Use the installed package

Add the installation prefix to `CMAKE_PREFIX_PATH`, then:

```cmake
find_package(scrydantic 0.1 CONFIG REQUIRED)
target_link_libraries(my_app PRIVATE scrydantic::scrydantic)
```

The target enables C++26 and reflection and links the compiled JSON core.
`scrydantic::json` exposes only that C++23 core without enabling reflection.

```cpp
#include <scrydantic/scrydantic.hpp>

namespace tools {
[[= scrydantic::tool{"Add two integers"}]]
int add(int a, int b) { return a + b; }
}

int main() {
  auto bound = scrydantic::bind<^^tools>();
  auto result = bound.front().invoke(
      scrydantic::Json{.text = R"({"a":2,"b":3})"});
  return result && result->text == "5" ? 0 : 1;
}
```

`bind()` supplies each tool's name, description, input schema, and callable
handler. `describe()` supplies compile-time contracts without handlers.
`encode()`, `decode<T>()`, and `input_schema_v<T>` also work independently
of tool binding. Fallible operations use `scrydantic::Result<T>` or `Status`;
a tool can refuse a call with `scrydantic::tool_error()`.

A library that builds on Scrydantic can give its own users the same
compile-time errors: each concept, such as `Encodable`, has a function in
`<scrydantic/diagnostics.hpp>`, such as `encodable_diagnostic<T>()`, that
returns why a type fails the check, or an empty string when it passes.

See [the MCP tools example](examples/mcp_tools.cpp) for an MCP-style
tool listing and calls; the example is not a complete MCP server.
[docs/architecture.md](docs/architecture.md) describes the design and its open
questions.

## License

[MIT](LICENSE).
