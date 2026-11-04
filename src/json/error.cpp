#include "json/core.hpp" // IWYU pragma: keep

#include "json/document.hpp"

#include <algorithm>
#include <cstddef>
#include <scrydantic/error.hpp>
#include <span>
#include <string>
#include <string_view>
#include <variant>

namespace scrydantic {
namespace {

// Whether `key` is an ASCII identifier, the keys a path spells in dot shorthand.
// Keys default to C++ member names, so this covers the common case; RFC 9535's
// shorthand also admits non-ASCII letters, but quoting every other key keeps the
// rule one sentence long.
[[nodiscard]] bool is_identifier(const std::string_view key) noexcept {
  const auto starts_word = [](const char character) {
    return (character >= 'a' && character <= 'z') ||
           (character >= 'A' && character <= 'Z') || character == '_';
  };
  const auto continues_word = [&starts_word](const char character) {
    return starts_word(character) || (character >= '0' && character <= '9');
  };
  return !key.empty() && starts_word(key.front()) &&
         std::ranges::all_of(key, continues_word);
}

// Appends one segment of a path: `.key`, `["key"]`, or `[index]`.
struct segment_writer {
  std::string& text;

  void operator()(const std::string& key) const {
    if (is_identifier(key)) {
      text.push_back('.');
      text.append(key);
      return;
    }
    text.push_back('[');
    detail::json::append_quoted(text, key);
    text.push_back(']');
  }

  void operator()(const std::size_t index) const {
    text.push_back('[');
    text.append(std::to_string(index));
    text.push_back(']');
  }
};

} // namespace

std::string json_path(const std::span<const PathSegment> path) {
  std::string text{"$"};
  for (const auto& segment : path) {
    std::visit(segment_writer{.text = text}, segment);
  }
  return text;
}

} // namespace scrydantic
