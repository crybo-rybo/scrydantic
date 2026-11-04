#include "json/core.hpp" // IWYU pragma: keep

#include "json/document.hpp"
#include "json/number.hpp"

#include <cmath>
#include <scrydantic/detail/json_text.hpp>
#include <string>
#include <string_view>

namespace scrydantic::detail {

bool validate_json(const std::string_view input) noexcept {
  return json::validate(input);
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
