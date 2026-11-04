# Scrydantic

Scrydantic uses C++26 static reflection to generate JSON Schema, strictly
encode and decode C++ values, and bind annotated C++ functions as tools that a
model or any other JSON client can call. Declare plain structs and functions,
annotate them, and get a schema, a strict decoder that enforces the annotated
constraints and reports every issue with its path, a canonical encoder, and a
type-erased handler, with no code generation step and no dependency beyond the
standard library.

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
of tool binding. A model can be recursive, through `std::vector<T>` or
`std::optional<std::indirect<T>>`. Its schema refers to the recursive types
with `$ref`, and every other schema is written in place. Fallible operations
use `scrydantic::Result<T>` or `Status`; a tool can refuse a call with
`scrydantic::tool_error()`. A decode failure lists each issue that it found in
`Error::issues`, with an `IssueKind` and a path, and its messages hold one line
for each, such as `$.shapes[0].radius must be a number`. A bound tool returns
those lines to the model in `model_message`.

Constraint annotations bound a member in its schema and in decoding at once, and
`check` runs a validator of your own on the decoded value:

```cpp
inline scrydantic::Status known_city(const std::string& city) {
  if (city == "Oslo" || city == "Lima") {
    return {};
  }
  return std::unexpected(
      scrydantic::tool_error("must be a known city", "has no row in cities"));
}

struct Booking {
  [[= scrydantic::check<&known_city>]] std::string city;
  [[= scrydantic::range{.min = 1, .max = 8}]] std::int32_t guests{};
  [[= scrydantic::length{.max = 200}]] std::string note;
};
```

The schema of `guests` is `{"maximum":8,"minimum":1,"type":"integer"}`, and
decoding `{"city":"Rome","guests":9,"note":""}` tells the model
`$.city must be a known city` and `$.guests must be at most 8`. The host's
`message` has `$.city has no row in cities` in the first line's place: a
validator's host text never reaches the model. `multiple_of`, `items`, and
`pattern` complete the constraints; `pattern` is written to the schema but not
checked. A class can carry `check` too, and a constraint that cannot hold, such
as `range{.min = -1}` on an unsigned member, does not compile. Constraints and
validators see through `std::indirect` and apply at every level of a recursive
model. Encoding checks nothing, since an encoded value is the program's own.

See [the MCP tools example](examples/mcp_tools.cpp) for an MCP-style
tool listing and calls; the example is not a complete MCP server.
[docs/architecture.md](docs/architecture.md) describes the design and its open
questions.

## License

[MIT](LICENSE).
