#include <cstdint>
#include <scrydantic/tools.hpp>

namespace range_tools {
// scrydantic::name gives one parameter the key another already has.
[[= scrydantic::tool{"Measure a span"}]] inline std::int32_t
    span(std::int32_t from, [[= scrydantic::name{"from"}]] std::int32_t until) {
  return until - from;
}
} // namespace range_tools

auto bind_tools() { return scrydantic::bind<^^range_tools>(); }
