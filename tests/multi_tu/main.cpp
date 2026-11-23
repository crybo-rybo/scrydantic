// A binding generates code from the declarations its translation unit can see.
// Two units that see different declarations under one name must each bind what
// they see, whichever order their object files are linked in, so this executable
// is built once per link order. It links Scrydantic alone.

#include "inventory.hpp"

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace echo_tools {
// Each unit declares echo with its own parameter name; this is the definition.
std::int32_t echo(std::int32_t value) { return value; }
// The same for twice, which each unit binds by hand, without a tool annotation.
std::int32_t twice(std::int32_t value) { return 2 * value; }
// Each unit declares capped with its own range on the parameter.
std::int32_t capped(std::int32_t value) { return value; }
// Unit A alone declares padded with a default argument, 3, for `pad`.
std::int32_t padded(std::int32_t count, std::int32_t pad) { return count + pad; }
} // namespace echo_tools

namespace {

[[nodiscard]] std::vector<std::string>
names_of(const std::vector<scrydantic::BoundTool<>>& tools) {
  std::vector<std::string> names{};
  for (const auto& tool : tools) {
    names.push_back(tool.name);
  }
  return names;
}

// Whether `tools` is echo alone, as one unit declared it: its description, its
// parameter name in the schema, and a handler that decodes that name.
[[nodiscard]] bool is_echo(std::vector<scrydantic::BoundTool<>> tools,
                           const std::string& description,
                           const std::string& parameter) {
  if (tools.size() != 1 || tools.front().name != "echo" ||
      tools.front().description != description) {
    return false;
  }
  const auto expected_schema =
      R"({"additionalProperties":false,"properties":{")" + parameter +
      R"(":{"maximum":2147483647,"minimum":-2147483648,"type":"integer"}},"required":[")" +
      parameter + R"("],"type":"object"})";
  const auto echoed =
      tools.front().invoke(scrydantic::Json{.text = "{\"" + parameter + "\":7}"});
  return tools.front().input_schema.text == expected_schema && echoed &&
         echoed->text == "7";
}

// Whether `bind` is twice as one unit declared it: its parameter name in the
// schema of the argument object, a call that decodes that name, and none that
// decodes the name the other unit gave it.
[[nodiscard]] bool is_twice(HandBound (*const bind)(std::string_view),
                            const std::string& parameter, const std::string& other) {
  const auto expected_schema =
      R"({"additionalProperties":false,"properties":{")" + parameter +
      R"(":{"maximum":2147483647,"minimum":-2147483648,"type":"integer"}},"required":[")" +
      parameter + R"("],"type":"object"})";
  const HandBound called = bind("{\"" + parameter + "\":7}");
  return called.schema == expected_schema && called.result == 14 &&
         !bind("{\"" + other + "\":7}").result;
}

// Whether a binding of capped, a tool or one made by hand, is capped as one
// unit declared it: `cap` is in the schema, a call at the cap goes through, and
// one past it does not.
[[nodiscard]] bool is_capped(const std::vector<scrydantic::BoundTool<>>& tools,
                             HandBound (*const bind)(std::string_view),
                             const std::int32_t cap) {
  const auto arguments = [](const std::int32_t count) {
    return "{\"count\":" + std::to_string(count) + "}";
  };
  const auto expected_schema =
      R"({"additionalProperties":false,"properties":{"count":{"maximum":)" +
      std::to_string(cap) +
      R"(,"minimum":-2147483648,"type":"integer"}},"required":["count"],"type":"object"})";
  if (tools.size() != 1 || tools.front().input_schema.text != expected_schema ||
      bind(arguments(cap)).schema != expected_schema) {
    return false;
  }
  const auto& invoke = tools.front().invoke;
  const auto within = invoke(scrydantic::Json{.text = arguments(cap)});
  const auto past = invoke(scrydantic::Json{.text = arguments(cap + 1)});
  return within && within->text == std::to_string(cap) && !past &&
         bind(arguments(cap)).result == cap && !bind(arguments(cap + 1)).result;
}

// Whether a binding of padded, a tool or one made by hand, is as one unit
// declared it: `pad` is required unless that unit gave it a default argument,
// and a call that leaves it out goes through with that default, or not at all.
[[nodiscard]] bool is_padded(const std::vector<scrydantic::BoundTool<>>& tools,
                             HandBound (*const bind)(std::string_view),
                             const bool defaulted) {
  const std::string integer =
      R"({"maximum":2147483647,"minimum":-2147483648,"type":"integer"})";
  const auto expected_schema =
      R"({"additionalProperties":false,"properties":{"count":)" + integer +
      R"(,"pad":)" + integer + R"(},"required":["count")" +
      (defaulted ? "" : R"(,"pad")") + R"(],"type":"object"})";
  if (tools.size() != 1 || tools.front().input_schema.text != expected_schema ||
      bind("{}").schema != expected_schema) {
    return false;
  }
  const auto& invoke = tools.front().invoke;
  const auto both = invoke(scrydantic::Json{.text = R"({"count":1,"pad":1})"});
  const auto one = invoke(scrydantic::Json{.text = R"({"count":1})"});
  if (!both || both->text != "2" || bind(R"({"count":1,"pad":1})").result != 2) {
    return false;
  }
  const auto by_hand = bind(R"({"count":1})").result;
  return defaulted ? one && one->text == "4" && by_hand == 4 : !one && !by_hand;
}

} // namespace

int main() {
  using Names = std::vector<std::string>;
  int failures = 0;
  failures += names_of(bind_inventory_from_a()) != Names{"only_a"} ? 1 : 0;
  failures += names_of(bind_inventory_from_b()) != Names{"only_b"} ? 1 : 0;
  failures += is_echo(bind_echo_from_a(), "Echo a count", "count") ? 0 : 1;
  failures += is_echo(bind_echo_from_b(), "Echo a quantity", "quantity") ? 0 : 1;
  failures += is_twice(twice_from_a, "count", "quantity") ? 0 : 1;
  failures += is_twice(twice_from_b, "quantity", "count") ? 0 : 1;
  failures += is_twice(wrapped_twice_from_a, "count", "quantity") ? 0 : 1;
  failures += is_twice(wrapped_twice_from_b, "quantity", "count") ? 0 : 1;
  failures += is_capped(bind_capped_from_a(), wrapped_capped_from_a, 9) ? 0 : 1;
  failures += is_capped(bind_capped_from_b(), wrapped_capped_from_b, 99) ? 0 : 1;
  failures += is_padded(bind_padded_from_a(), wrapped_padded_from_a, true) ? 0 : 1;
  failures += is_padded(bind_padded_from_b(), wrapped_padded_from_b, false) ? 0 : 1;
  return failures == 0 ? 0 : 1;
}
