#include "inventory.hpp"

#include <cstdint>
#include <string>
#include <string_view>
#include <utility>

namespace inventory_tools {
[[= scrydantic::tool{"Only unit A declares this tool"}]] inline std::string only_a() {
  return "a";
}
} // namespace inventory_tools

namespace echo_tools {
[[= scrydantic::tool{"Echo a count"}]] std::int32_t echo(std::int32_t count);
std::int32_t twice(std::int32_t count);
[[= scrydantic::tool{"Echo a capped count"}]] std::int32_t
    capped([[= scrydantic::range{.max = 9}]] std::int32_t count);
[[= scrydantic::tool{"Echo a padded count"}]] std::int32_t padded(std::int32_t count,
                                                                  std::int32_t pad = 3);
} // namespace echo_tools

std::vector<scrydantic::BoundTool<>> bind_inventory_from_a() {
  return scrydantic::bind<^^inventory_tools>();
}

std::vector<scrydantic::BoundTool<>> bind_echo_from_a() {
  return scrydantic::bind<^^echo_tools::echo>();
}

std::vector<scrydantic::BoundTool<>> bind_capped_from_a() {
  return scrydantic::bind<^^echo_tools::capped>();
}

std::vector<scrydantic::BoundTool<>> bind_padded_from_a() {
  return scrydantic::bind<^^echo_tools::padded>();
}

HandBound twice_from_a(const std::string_view arguments) {
  using Arguments = scrydantic::arguments_t<^^echo_tools::twice>;
  HandBound bound{.schema = scrydantic::input_schema_v<Arguments>};
  if (auto decoded = scrydantic::decode<Arguments>(arguments)) {
    bound.result = scrydantic::apply<^^echo_tools::twice>(std::move(*decoded));
  }
  return bound;
}

HandBound wrapped_twice_from_a(const std::string_view arguments) {
  return bind_by_hand<^^echo_tools::twice>(arguments);
}

HandBound wrapped_capped_from_a(const std::string_view arguments) {
  return bind_by_hand<^^echo_tools::capped>(arguments);
}

HandBound wrapped_padded_from_a(const std::string_view arguments) {
  return bind_by_hand<^^echo_tools::padded>(arguments);
}
