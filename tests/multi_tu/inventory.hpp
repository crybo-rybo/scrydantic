#pragma once

#include <cstdint>
#include <meta>
#include <optional>
#include <scrydantic/tools.hpp>
#include <string_view>
#include <utility>
#include <vector>

// Two translation units bind tools under one name while seeing different
// declarations: each declares a different part of namespace inventory_tools, and
// declares echo_tools::echo with its own parameter name and description.
namespace inventory_tools {}

[[nodiscard]] std::vector<scrydantic::BoundTool<>> bind_inventory_from_a();
[[nodiscard]] std::vector<scrydantic::BoundTool<>> bind_inventory_from_b();
[[nodiscard]] std::vector<scrydantic::BoundTool<>> bind_echo_from_a();
[[nodiscard]] std::vector<scrydantic::BoundTool<>> bind_echo_from_b();

// Each unit declares echo_tools::capped with one name, one description, and one
// parameter name, and its own range on the parameter: the annotations are all
// that the two units see differently.
[[nodiscard]] std::vector<scrydantic::BoundTool<>> bind_capped_from_a();
[[nodiscard]] std::vector<scrydantic::BoundTool<>> bind_capped_from_b();

// Each unit declares echo_tools::padded with the same parameters, and unit A
// alone gives the second a default argument, so only A may leave it out.
[[nodiscard]] std::vector<scrydantic::BoundTool<>> bind_padded_from_a();
[[nodiscard]] std::vector<scrydantic::BoundTool<>> bind_padded_from_b();

// Each unit also declares echo_tools::twice, which is no tool, with its own
// parameter name, and binds it by hand: the schema of its argument object as
// that unit sees it, and what a call with `arguments` returned, or nothing when
// they did not decode.
struct HandBound {
  std::string_view schema{};
  std::optional<std::int32_t> result{};
};

// A wrapper of arguments_t and apply(), as a library on top of Scrydantic writes
// one. Both units instantiate this one template, so only the `Binding` that it
// takes from its caller and forwards keeps their two views of a function apart.
template <std::meta::info Function,
          typename Binding = typename[:scrydantic::binding_of(Function, ^^void):]>
[[nodiscard]] HandBound bind_by_hand(const std::string_view arguments) {
  using Arguments = scrydantic::arguments_t<Function, void, Binding>;
  HandBound bound{.schema = scrydantic::input_schema_v<Arguments>};
  if (auto decoded = scrydantic::decode<Arguments>(arguments)) {
    bound.result = scrydantic::apply<Function, void, Binding>(std::move(*decoded));
  }
  return bound;
}

// Through arguments_t and apply() themselves.
[[nodiscard]] HandBound twice_from_a(std::string_view arguments);
[[nodiscard]] HandBound twice_from_b(std::string_view arguments);
// Through bind_by_hand().
[[nodiscard]] HandBound wrapped_twice_from_a(std::string_view arguments);
[[nodiscard]] HandBound wrapped_twice_from_b(std::string_view arguments);
[[nodiscard]] HandBound wrapped_capped_from_a(std::string_view arguments);
[[nodiscard]] HandBound wrapped_capped_from_b(std::string_view arguments);
[[nodiscard]] HandBound wrapped_padded_from_a(std::string_view arguments);
[[nodiscard]] HandBound wrapped_padded_from_b(std::string_view arguments);
