// The README's package example: bind a namespace and call its one tool. It
// also checks that the version macros match the version CMake found.

#include <scrydantic/scrydantic.hpp>
#include <string_view>

static_assert(std::string_view{SCRYDANTIC_VERSION_STRING} ==
              SCRYDANTIC_EXPECTED_VERSION);

namespace tools {
[[= scrydantic::tool{"Add two integers"}]] int add(int a, int b) { return a + b; }
} // namespace tools

int main() {
  auto bound = scrydantic::bind<^^tools>();
  auto result = bound.front().invoke(scrydantic::Json{.text = R"({"a":2,"b":3})"});
  return result && result->text == "5" ? 0 : 1;
}
