#pragma once

#include <algorithm>
#include <array>
#include <charconv>
#include <cmath>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <meta>
#include <scrydantic/detail/failure.hpp>
#include <scrydantic/detail/json_string.hpp>
#include <scrydantic/detail/json_text.hpp>
#include <scrydantic/detail/model.hpp>
#include <scrydantic/error.hpp>
#include <scrydantic/json.hpp>
#include <string>
#include <string_view>
#include <system_error>
#include <type_traits>
#include <utility>
#include <variant>

// Reflected encoding. The writer emits objects in canonical key order and strings
// with canonical escapes directly, so the only parts of its output whose spelling
// is a choice are numbers and spliced Json payloads; `spelling` makes that choice.
// Every function here is reached only through an entry point that has already
// checked the type against a value family.
//
// What the writer appends is JSON as the parser accepts it, by construction, so
// no pass over the finished text checks it:
//
// - Structure, `true`, `false`, and `null` are the writer's own bytes.
// - A number is std::to_chars' text of an integer or of a finite float or
//   double, or the canonical spelling of one; a non-finite number is refused.
// - A string is checked to be UTF-8 as it is escaped. Keys, tags, and
//   enumerator names are escaped at compile time, and checked then.
// - A Json payload is read by the parser's own rules, with the arrays and
//   objects open around it counted toward the depth limit.
// - The writer refuses the array or object that would pass the depth limit.
//
// tests/codec/codec_tests.cpp holds the output to the parser's rules.
namespace scrydantic::detail {

// How the writer spells floating-point numbers and spliced Json payloads.
//
// `verbatim` writes std::to_chars' shortest text of a number and splices a Json
// payload byte for byte, after checking that it is JSON. Bound tool results use
// it, as do hosts that must keep stored payload bytes as they are.
//
// `canonical` writes what canonicalizing the verbatim text would write, so
// encode() needs no canonicalizing parse of the whole text.
// tests/codec/codec_tests.cpp holds the two to the same bytes.
enum class spelling : std::uint8_t {
  verbatim,
  canonical,
};

// `depth` counts the arrays and objects open around `value`.
template <typename Type, spelling Spelling = spelling::verbatim>
[[nodiscard]] encode_result<void> write_value(std::string& output, const Type& value,
                                              std::size_t depth);

// A recursive value can nest without bound, so the writer refuses the array or
// object that the parser would refuse, before its own recursion can exhaust the
// stack. The writers test the depth themselves and call this only to fail: an
// encode_result<void> built and tested for every container cost 2% of an encode.
[[nodiscard]] inline std::unexpected<encode_failure> nested_too_deep() {
  return encode_fail("is an array or object nested more than " +
                     std::to_string(json_max_depth) +
                     " deep, so the value cannot be JSON");
}

// The two failures that are about the value as a whole. Their words say what a
// check of the finished text would find, which is how they were first reported.
[[nodiscard]] inline std::unexpected<encode_failure> not_utf8() {
  return encode_fail_whole("the value holds a string that is not valid UTF-8, so it "
                           "cannot be JSON");
}

[[nodiscard]] inline std::unexpected<encode_failure> text_too_deep() {
  return encode_fail_whole("the value nests arrays and objects more than " +
                           std::to_string(json_max_depth) +
                           " deep, so it cannot be JSON");
}

// Whether the parser reads `text`, std::to_chars' spelling of a finite number,
// as an integer: digits alone, after at most a '-', that fit uint64 when not
// negative and int64 when negative. "-0" does not; it is the double -0.0.
[[nodiscard]] inline bool reads_as_integer(const std::string_view text) {
  if (text.find_first_of(".e") != std::string_view::npos) {
    return false;
  }
  const char* const first = text.data();
  const char* const last = first + text.size();
  if (text.front() == '-') {
    std::int64_t integer{};
    const auto [end, error] = std::from_chars(first, last, integer);
    return error == std::errc{} && end == last && integer != 0;
  }
  std::uint64_t integer{};
  const auto [end, error] = std::from_chars(first, last, integer);
  return error == std::errc{} && end == last;
}

// The canonical spelling of a finite float or double whose shortest text is
// `text`, as a canonicalizing parse of that text writes it: text the parser
// reads as an integer keeps its digits, and anything else is respelled as the
// double the text denotes. For a double that is the value itself, since its
// shortest text round-trips; a float's text denotes the nearest double to it.
template <std::floating_point Float>
void append_canonical_float(std::string& output, const Float value,
                            const std::string_view text) {
  if (reads_as_integer(text)) {
    output.append(text);
    return;
  }
  double number{};
  if constexpr (std::same_as<Float, double>) {
    number = value;
  } else {
    // Cannot fail: the text is std::to_chars' spelling of a finite float.
    static_cast<void>(std::from_chars(text.data(), text.data() + text.size(), number));
  }
  append_canonical_json_number(output, number);
}

template <spelling Spelling, typename Number>
[[nodiscard]] encode_result<void> write_number(std::string& output,
                                               const Number value) {
  if constexpr (std::floating_point<Number>) {
    if (!std::isfinite(value)) {
      return encode_fail("must be finite");
    }
  }
  // Wide enough for any 64-bit integer and for the shortest round-trip form of any
  // double (at most 24 characters), so to_chars cannot run out of room. With no
  // precision argument it writes the shortest text that reads back as the value.
  std::array<char, 32> buffer{};
  const auto written =
      std::to_chars(buffer.data(), buffer.data() + buffer.size(), value);
  const std::string_view text{buffer.data(), written.ptr};
  if constexpr (Spelling == spelling::canonical && std::floating_point<Number>) {
    append_canonical_float(output, value, text);
  } else {
    output.append(text);
  }
  return {};
}

// A string is escaped and checked to be UTF-8 in one pass over it.
[[nodiscard]] inline encode_result<void> write_string(std::string& output,
                                                      const std::string_view value) {
  if (!append_utf8_json_string(output, value)) [[unlikely]] {
    return not_utf8();
  }
  return {};
}

// Whether every enumerator of Enum has a name that is UTF-8. An identifier is,
// wherever the ordinary literal encoding is UTF-8.
template <typename Enum>
inline constexpr bool utf8_names_v =
    std::ranges::all_of(enumerators_v<Enum>, [](const std::meta::info enumerator) {
      return is_utf8(std::meta::identifier_of(enumerator));
    });

template <typename Enum>
[[nodiscard]] encode_result<void> write_enum(std::string& output, const Enum value) {
  if constexpr (!utf8_names_v<Enum>) {
    return not_utf8();
  }
  template for (constexpr std::meta::info enumerator : enumerators_v<Enum>) {
    if (value == [:enumerator:]) {
      output.append(quoted(std::meta::identifier_of(enumerator)));
      return {};
    }
  }
  return encode_fail("is not a declared enumerator value");
}

// A Json value is checked and then written byte for byte, or canonicalized; the
// caller owns its verbatim spelling. `depth` arrays and objects are open around
// it, and the ones it nests are counted on top of them, in the text that is
// written: canonical text keeps only the last of an object's members of one
// key, so it can be within the limit where the payload's own text is not.
template <spelling Spelling>
[[nodiscard]] encode_result<void>
write_json_value(std::string& output, const Json& value, const std::size_t depth) {
  json_validity validity{};
  if constexpr (Spelling == spelling::canonical) {
    validity = append_canonical_json(output, value.text, depth);
  } else {
    validity = check_json(value.text, depth);
    if (validity == json_validity::valid) {
      output.append(value.text);
    }
  }
  if (validity == json_validity::valid) [[likely]] {
    return {};
  }
  // Reading stopped where the payload nests too deep for where it stands, but
  // it may not be JSON anywhere: it may nest too deep alone, or break another
  // rule further on.
  if (validity == json_validity::too_deep && depth != 0 && is_valid_json(value.text)) {
    return text_too_deep();
  }
  return encode_fail("is not valid JSON");
}

template <spelling Spelling, typename Sequence>
[[nodiscard]] encode_result<void>
write_sequence(std::string& output, const Sequence& value, const std::size_t depth) {
  if (depth == json_max_depth) [[unlikely]] {
    return nested_too_deep();
  }
  output.push_back('[');
  for (std::size_t index = 0; index < value.size(); ++index) {
    if (index != 0) {
      output.push_back(',');
    }
    if (auto status = write_value<typename Sequence::value_type, Spelling>(
            output, value[index], depth + 1);
        !status) {
      return std::unexpected(at_element(std::move(status.error()), index));
    }
  }
  output.push_back(']');
  return {};
}

// Whether the keys of Type's members are UTF-8, and its tag too when Tagged. A
// scrydantic::name or scrydantic::tag can hold any bytes.
template <typename Type, bool Tagged>
inline constexpr bool utf8_keys_v =
    (!Tagged || is_utf8(tag_of(^^Type).value_or(""))) &&
    std::ranges::all_of(fields_v<Type>, [](const field_layout& field) {
      return is_utf8(field.key_view());
    });

// Tagged is true when the object is a variant alternative, whose tag member joins
// its members at its lexical position: before the first key that sorts after it,
// or last.
template <typename Type, bool Tagged, spelling Spelling>
[[nodiscard]] encode_result<void> write_object(std::string& output, const Type& value,
                                               const std::size_t depth) {
  if (depth == json_max_depth) [[unlikely]] {
    return nested_too_deep();
  }
  if constexpr (!utf8_keys_v<Type, Tagged>) {
    return not_utf8();
  }
  output.push_back('{');
  bool first = true;
  const auto separate = [&output, &first] {
    if (!first) {
      output.push_back(',');
    }
    first = false;
  };
  [[maybe_unused]] bool tag_pending = Tagged;
  [[maybe_unused]] const auto write_tag = [&] {
    // Discarded for an untagged object, which has no tag member to spell.
    if constexpr (Tagged) {
      if (tag_pending) {
        separate();
        output.append(tag_member(^^Type));
        tag_pending = false;
      }
    }
  };
  template for (constexpr field_layout field : fields_v<Type>) {
    constexpr std::meta::info member = field.member;
    constexpr std::string_view key = field.key_view();
    if constexpr (Tagged && key > tag_key) {
      write_tag();
    }
    bool present = true;
    if constexpr (field.omit_null) {
      present = value.[:member:].has_value();
    }
    if (present) {
      separate();
      output.append(member_prefix(key));
      using Member = std::remove_cvref_t<decltype(value.[:member:])>;
      if (auto status =
              write_value<Member, Spelling>(output, value.[:member:], depth + 1);
          !status) {
        return std::unexpected(at_member(std::move(status.error()), key));
      }
    }
  }
  if constexpr (Tagged) {
    write_tag();
  }
  output.push_back('}');
  return {};
}

template <spelling Spelling, typename Variant>
[[nodiscard]] encode_result<void>
write_variant(std::string& output, const Variant& value, const std::size_t depth) {
  if (value.valueless_by_exception()) {
    return encode_fail("is a variant left valueless by an exception");
  }
  return std::visit(
      [&output, depth]<typename Alternative>(const Alternative& alternative) {
        return write_object<Alternative, true, Spelling>(output, alternative, depth);
      },
      value);
}

// An indirect is written as the value it owns, which a move leaves it without.
template <spelling Spelling, typename Indirect>
[[nodiscard]] encode_result<void>
write_indirect(std::string& output, const Indirect& value, const std::size_t depth) {
  if (value.valueless_after_move()) {
    return encode_fail("is a std::indirect left valueless by a move");
  }
  return write_value<typename Indirect::value_type, Spelling>(output, *value, depth);
}

template <typename Type, spelling Spelling>
encode_result<void> write_value(std::string& output, const Type& value,
                                const std::size_t depth) {
  constexpr auto kind = kind_of(^^Type);
  if constexpr (kind == value_kind::boolean) {
    output.append(value ? "true" : "false");
    return {};
  } else if constexpr (kind == value_kind::integer || kind == value_kind::floating) {
    return write_number<Spelling>(output, value);
  } else if constexpr (kind == value_kind::string || kind == value_kind::string_view) {
    return write_string(output, value);
  } else if constexpr (kind == value_kind::json) {
    return write_json_value<Spelling>(output, value, depth);
  } else if constexpr (kind == value_kind::enumeration) {
    return write_enum(output, value);
  } else if constexpr (kind == value_kind::optional) {
    if (!value.has_value()) {
      output.append("null");
      return {};
    }
    return write_value<typename Type::value_type, Spelling>(output, *value, depth);
  } else if constexpr (kind == value_kind::indirect) {
    return write_indirect<Spelling>(output, value, depth);
  } else if constexpr (kind == value_kind::vector || kind == value_kind::array) {
    return write_sequence<Spelling>(output, value, depth);
  } else if constexpr (kind == value_kind::variant) {
    return write_variant<Spelling>(output, value, depth);
  } else {
    return write_object<Type, false, Spelling>(output, value, depth);
  }
}

// Encodes a value checked against a family. Failures are host-only diagnostics,
// with no model_message: the value is the program's own, not the model's. They
// are rooted at `$`, but for a string that is not UTF-8 and for Json payloads
// that nest too deep where they stand, which are reported of the whole value.
// The text is valid JSON, as the top of this file explains.
//
// `Type` may be a reference or cv-qualified, as `encode<T>(value)` with a
// forwarded T makes it; the writer sees the value type alone.
template <typename Type, spelling Spelling = spelling::verbatim>
[[nodiscard]] Result<Json> encode_value(const Type& value) {
  std::string output{};
  if (auto status = write_value<std::remove_cvref_t<Type>, Spelling>(output, value, 0);
      !status) {
    return std::unexpected(encode_failure_error(std::move(status.error())));
  }
  return Json{.text = std::move(output)};
}

} // namespace scrydantic::detail
