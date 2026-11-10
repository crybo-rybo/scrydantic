# Scrydantic

Pydantic-style JSON for C++26. Declare a plain struct and Scrydantic uses
static reflection to give you its JSON Schema, a strict decoder that reports
every problem with its path, and a canonical encoder. Annotate a function and
it becomes a tool that a model, or any other JSON client, can call.

There is no code generator, no macros, and no dependency beyond the standard
library.

```cpp
#include <scrydantic/scrydantic.hpp>

struct [[= scrydantic::tag{"circle"}]] Circle { double radius; };
struct [[= scrydantic::tag{"square"}]] Square { double side; };

struct Drawing {
  std::vector<std::variant<Circle, Square>> shapes;
};

// The schema is a compile-time string.
constexpr std::string_view schema = scrydantic::schema_v<Drawing>;

// Decoding is strict, and one call reports every issue it finds.
auto drawing = scrydantic::decode<Drawing>(
    R"({"shapes":[{"type":"circle","radius":"big"},{"type":"hex"}]})");
// drawing.error().message:
//   $.shapes[0].radius must be a number
//   $.shapes[1].type must be one of: circle, square

// Encoding writes canonical JSON.
auto json = scrydantic::encode(Drawing{.shapes = {Circle{1.5}, Square{2}}});
// json->text:
//   {"shapes":[{"radius":1.5,"type":"circle"},{"side":2,"type":"square"}]}
```

Tools work the same way. The parameter list becomes the input schema, and the
handler decodes the arguments, calls the function, and encodes the result:

```cpp
namespace tools {
[[= scrydantic::tool{"Add two integers"}]]
int add(int a, int b) { return a + b; }
}

auto bound = scrydantic::bind<^^tools>();
bound[0].name;                    // "add"
bound[0].input_schema.text;       // {"additionalProperties":false,"properties":{"a":...
bound[0].invoke(scrydantic::Json{.text = R"({"a":2,"b":3})"})->text;  // "5"
```

**[Read the guide](docs/guide.md)** for the supported types, the annotations,
constraints and validators, error handling, and tool binding.
[`examples/mcp_tools.cpp`](examples/mcp_tools.cpp) serves MCP-style
`tools/list` and `tools/call` requests from a few annotated functions.

## Requirements

- GCC 16.2 or newer. It is the only compiler that implements C++26 reflection.
- CMake 3.30 or newer.

## Add it to your project

With FetchContent:

```cmake
include(FetchContent)
FetchContent_Declare(
  scrydantic
  GIT_REPOSITORY https://github.com/crybo-rybo/scrydantic.git
  GIT_TAG v0.2.0
)
FetchContent_MakeAvailable(scrydantic)
target_link_libraries(my_app PRIVATE scrydantic::scrydantic)
```

Or install it (`cmake --install build/dev --prefix <prefix>`) and use
`find_package(scrydantic 0.2 CONFIG REQUIRED)`.

The `scrydantic::scrydantic` target turns on C++26 and `-freflection` for you.
Scrydantic is pre-1.0, so a minor release can change the API.

## Build and test

```sh
cmake --workflow --preset dev    # configure, build, and run every test
scripts/check.sh                 # formatting, static checks, and the tests
scripts/format.sh --fix          # format the sources
```

Set `CXX` on the first configure if `g++` is not GCC 16.2. The other presets
are `release`, `asan`, `tsan`, and `coverage`.

## License

[MIT](LICENSE)
