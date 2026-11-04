// Property fuzz target for Scrydantic's JSON core (src/json/document.hpp). For every
// input it asserts that
//   - validate() accepts exactly what parse() accepts;
//   - an accepted document's canonical text parses, validates, and describes the
//     same tree: the same kinds, keys, strings, and numeric values, where a
//     whole-valued double such as 1.0 may come back as the equal integer 1,
//     since canonical text spells it that way;
//   - canonicalization is idempotent: the canonical text writes back as itself;
//   - every object's keys are strictly increasing and find() reaches each one;
//   - quoting the raw input yields a JSON string that, when it parses at all,
//     holds exactly the input, and always parses when the input is ASCII.

#include "json/document.hpp"

#include <algorithm>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <format>
#include <print>
#include <scrydantic/json.hpp>
#include <string>
#include <string_view>

namespace {

namespace json = scrydantic::detail::json;

// Reports a broken property with the input that broke it, printable ASCII as
// itself and every other byte, '%' included, as %HH, then aborts.
[[noreturn]] void fail(const std::string_view what, const std::string_view input) {
  std::string shown{};
  for (const char character : input) {
    const auto byte = static_cast<unsigned char>(character);
    if (byte >= 0x20U && byte < 0x7FU && byte != '%') {
      shown.push_back(character);
    } else {
      shown += std::format("%{:02X}", byte);
    }
  }
  std::println(stderr, "json fuzz: {}\ninput ({} bytes): {}", what, input.size(),
               shown);
  std::abort();
}

[[nodiscard]] bool is_numeric(const scrydantic::JsonKind kind) {
  return kind == scrydantic::JsonKind::signed_integer ||
         kind == scrydantic::JsonKind::unsigned_integer ||
         kind == scrydantic::JsonKind::number;
}

// A double and an integer are the same number only when the double is whole and
// converts to exactly that integer.
[[nodiscard]] bool double_equals_integer(const double value,
                                         const json::Value& integer) {
  constexpr double two_pow_63 = 9223372036854775808.0;
  if (value != std::trunc(value)) {
    return false;
  }
  if (const auto unsigned_value = integer.unsigned_integer()) {
    return value >= 0.0 && value < 2.0 * two_pow_63 &&
           static_cast<std::uint64_t>(value) == *unsigned_value;
  }
  const auto signed_value = integer.signed_integer();
  return signed_value && value >= -two_pow_63 && value < two_pow_63 &&
         static_cast<std::int64_t>(value) == *signed_value;
}

[[nodiscard]] bool same_number(const json::Value& left, const json::Value& right) {
  const auto left_number = left.number();
  const auto right_number = right.number();
  if (!left_number || !right_number) {
    return false;
  }
  const bool left_double = left.kind() == scrydantic::JsonKind::number;
  const bool right_double = right.kind() == scrydantic::JsonKind::number;
  if (left_double && right_double) {
    return std::bit_cast<std::uint64_t>(*left_number) ==
           std::bit_cast<std::uint64_t>(*right_number);
  }
  if (left_double) {
    return double_equals_integer(*left_number, right);
  }
  if (right_double) {
    return double_equals_integer(*right_number, left);
  }
  return left.kind() == right.kind() &&
         left.signed_integer() == right.signed_integer() &&
         left.unsigned_integer() == right.unsigned_integer();
}

[[nodiscard]] bool same_tree(const json::Value& left, const json::Value& right);

[[nodiscard]] bool same_containers(const json::Value& left, const json::Value& right) {
  if (left.size() != right.size()) {
    return false;
  }
  if (const auto* elements = left.array()) {
    for (std::size_t index = 0; index < elements->size(); ++index) {
      if (!same_tree((*elements)[index], (*right.array())[index])) {
        return false;
      }
    }
    return true;
  }
  const auto& right_members = *right.object();
  std::size_t index = 0;
  for (const auto& [key, value] : *left.object()) {
    const auto& [right_key, right_value] = right_members[index++];
    if (key != right_key || !same_tree(value, right_value)) {
      return false;
    }
  }
  return true;
}

bool same_tree(const json::Value& left, const json::Value& right) {
  if (is_numeric(left.kind()) || is_numeric(right.kind())) {
    return is_numeric(left.kind()) && is_numeric(right.kind()) &&
           same_number(left, right);
  }
  if (left.kind() != right.kind()) {
    return false;
  }
  if (left.array() != nullptr || left.object() != nullptr) {
    return same_containers(left, right);
  }
  return left.boolean() == right.boolean() && left.string() == right.string();
}

void check_objects(const json::Value& value, const std::string_view input) {
  if (const auto* elements = value.array()) {
    for (const auto& element : *elements) {
      check_objects(element, input);
    }
    return;
  }
  const auto* members = value.object();
  if (members == nullptr) {
    return;
  }
  for (std::size_t index = 0; index < members->size(); ++index) {
    const auto& [key, member] = (*members)[index];
    if (index > 0 && !((*members)[index - 1].first < key)) {
      fail("object keys are not strictly increasing", input);
    }
    if (value.find(key) != &member) {
      fail("find() misses a member", input);
    }
    check_objects(member, input);
  }
}

[[nodiscard]] bool is_ascii(const std::string_view text) {
  return std::ranges::all_of(text, [](const char character) {
    return static_cast<unsigned char>(character) < 0x80U;
  });
}

void check_quoting(const std::string_view input) {
  const auto quoted = json::quote(input);
  const auto parsed = json::parse(quoted);
  if (parsed ? parsed->string() != input : is_ascii(input)) {
    fail("a quoted string does not read back as itself", input);
  }
}

} // namespace

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data,
                                      const std::size_t size) {
  const auto input = std::string_view{reinterpret_cast<const char*>(data), size};
  check_quoting(input);

  const auto parsed = json::parse(input);
  if (parsed.has_value() != json::validate(input)) {
    fail("validate disagrees with parse", input);
  }
  if (!parsed) {
    return 0;
  }
  check_objects(*parsed, input);

  const auto canonical = json::write(*parsed);
  const auto reparsed = json::parse(canonical);
  if (!reparsed || !json::validate(canonical)) {
    fail("canonical text does not parse", input);
  }
  if (!same_tree(*parsed, *reparsed)) {
    fail("canonical text describes a different tree", input);
  }
  if (json::write(*reparsed) != canonical) {
    fail("canonicalization is not idempotent", input);
  }
  return 0;
}
