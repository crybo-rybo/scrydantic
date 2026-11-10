#include "json/core.hpp" // IWYU pragma: keep

#include "json/document.hpp"
#include "json/number.hpp"

#include <cmath>
#include <cstddef>
#include <scrydantic/detail/json_text.hpp>
#include <scrydantic/json.hpp>
#include <string>
#include <string_view>

namespace scrydantic {

bool is_valid_json(const std::string_view text) noexcept {
  return detail::json::validate(text);
}

std::string format_json_number(const double value) {
  std::string text{};
  detail::append_canonical_json_number(text, value);
  return text;
}

} // namespace scrydantic

namespace scrydantic::detail {

json_validity check_json(const std::string_view input,
                         const std::size_t depth) noexcept {
  return json::check(input, depth);
}

std::string describe_invalid_json(const std::string_view input) {
  const auto rejection = json::diagnose(input);
  return rejection ? json::describe(input, *rejection) : std::string{};
}

json_validity append_canonical_json(std::string& output, const std::string_view text,
                                    const std::size_t depth) {
  const auto document = json::parse(text);
  if (!document) {
    return json_validity::invalid;
  }
  // Measured on the document, not while reading the text: the text may nest
  // deeper in a member that a later one of the same key replaces.
  if (depth + json::nesting(*document) > json_max_depth) {
    return json_validity::too_deep;
  }
  json::write(*document, output);
  return json_validity::valid;
}

bool append_utf8_json_string(std::string& output, const std::string_view text) {
  return json::append_quoted_utf8(output, text);
}

void append_canonical_json_number(std::string& output, const double value) {
  if (!std::isfinite(value)) {
    output.append("null");
    return;
  }
  json::append_double(output, value);
}

} // namespace scrydantic::detail
