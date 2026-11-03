# Scrydantic

Scrydantic uses C++26 static reflection to generate JSON Schema, strictly
encode and decode C++ values, and bind annotated C++ functions as tools. It
builds independently of Scry and needs no libcurl, model, or network service.

Scrydantic was extracted from [Scry](https://github.com/crybo-rybo/scry),
which consumes it, and is developed in this repository.

## Build and test

Use GCC 16 or newer with reflection support, CMake 3.30 or newer, and Ninja.
From the repository root:

```sh
cmake -S . -B build -G Ninja -DCMAKE_CXX_COMPILER=g++-16
cmake --build build
ctest --test-dir build --output-on-failure
build/examples/scrydantic_standalone_tools_example
cmake --install build --prefix "$PWD/stage"
```

Set `CMAKE_CXX_COMPILER` to your compiler's name or path if it differs.
Tests fetch Catch2; the library itself has no third-party library dependencies.

The presets and scripts that CI runs are the same ones to use locally:

```sh
PRESET=dev ./scripts/test.sh    # also asan and tsan; CXX overrides g++-16
./scripts/format.sh --check     # or --fix; CLANG_FORMAT overrides clang-format
./scripts/lint.sh               # CLANG_TIDY overrides clang-tidy
```

CI uses clang-format and clang-tidy 22. clang-tidy checks only the C++23 JSON
core under `src/json/`, because it cannot parse the reflection code.

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

See [the standalone example](examples/standalone_tools.cpp) for an MCP-style
tool listing and calls, and [NOTES.md](NOTES.md) for the extraction design and
its open questions. The example is not a complete MCP server.

## License

[MIT](LICENSE).
