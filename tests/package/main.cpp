// The README's package example: bind a namespace and call its one tool.

#include <scrydantic/scrydantic.hpp>

namespace tools {
[[= scrydantic::tool{"Add two integers"}]] int add(int a, int b) { return a + b; }
} // namespace tools

int main() {
  auto bound = scrydantic::bind<^^tools>();
  auto result = bound.front().invoke(scrydantic::Json{.text = R"({"a":2,"b":3})"});
  return result && result->text == "5" ? 0 : 1;
}
