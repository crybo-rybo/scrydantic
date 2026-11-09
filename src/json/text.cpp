#include "json/core.hpp" // IWYU pragma: keep

#include "json/document.hpp"
#include "json/number.hpp"

#include <cmath>
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

json_validity check_json(const std::string_view input) noexcept {
  return json::check(input);
}

std::string describe_invalid_json(const std::string_view input) {
  const auto rejection = json::diagnose(input);
  return rejection ? json::describe(input, *rejection) : std::string{};
}

bool append_canonical_json(std::string& output, const std::string_view text) {
  const auto document = json::parse(text);
  if (!document) {
    return false;
  }
  json::write(*document, output);
  return true;
}

void append_canonical_json_number(std::string& output, const double value) {
  if (!std::isfinite(value)) {
    output.append("null");
    return;
  }
  json::append_double(output, value);
}

} // namespace scrydantic::detail
