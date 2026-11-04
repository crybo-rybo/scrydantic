#pragma once

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
namespace scrydantic::detail {

// How the writer spells floating-point numbers and spliced Json payloads.
//
// `verbatim` writes std::to_chars' shortest text of a number and splices a Json
// payload byte for byte, after checking that it is JSON. Bound tool results use
// it, as do hosts that must keep stored payload bytes as they are.
//
// `canonical` writes what canonicalizing the verbatim text would write, so
// encode() needs only an allocation-free validating pass after it, rather than a
// canonicalizing parse of the whole text. tests/codec/codec_tests.cpp holds the
// two to the same bytes.
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

template <typename Enum>
[[nodiscard]] encode_result<void> write_enum(std::string& output, const Enum value) {
  template for (constexpr std::meta::info enumerator : enumerators_v<Enum>) {
    if (value == [:enumerator:]) {
      output.append(quoted(std::meta::identifier_of(enumerator)));
      return {};
    }
  }
  return encode_fail("is not a declared enumerator value");
}

// A Json value is checked and then written byte for byte, or canonicalized; the
// caller owns its verbatim spelling.
template <spelling Spelling>
[[nodiscard]] encode_result<void> write_json_value(std::string& output,
                                                   const Json& value) {
  if constexpr (Spelling == spelling::canonical) {
    if (!append_canonical_json(output, value.text)) {
      return encode_fail("is not valid JSON");
    }
  } else {
    if (!validate_json(value.text)) {
      return encode_fail("is not valid JSON");
    }
    output.append(value.text);
  }
  return {};
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

// Tagged is true when the object is a variant alternative, whose tag member joins
// its members at its lexical position: before the first key that sorts after it,
// or last.
template <typename Type, bool Tagged, spelling Spelling>
[[nodiscard]] encode_result<void> write_object(std::string& output, const Type& value,
                                               const std::size_t depth) {
  if (depth == json_max_depth) [[unlikely]] {
    return nested_too_deep();
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
    append_json_string(output, value);
    return {};
  } else if constexpr (kind == value_kind::json) {
    return write_json_value<Spelling>(output, value);
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
// with no model_message, rooted at `$`. The text is valid JSON except where a
// string value holds invalid UTF-8, the one thing the writer copies unchecked,
// or where spliced Json payloads, each valid alone, nest past the parser's depth
// limit together; validate_encoded_json catches both.
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

// The check every encoding ends with. It builds no document, so a host can
// still bound an oversized tool result before paying for a canonicalizing parse.
// The writer escapes every control character, rejects non-finite numbers, checks
// each spliced Json payload, and bounds its own nesting, so its text can fail
// here only by a payload that nests too deep within what encloses it, or by
// holding a string, key, or tag that is not valid UTF-8. Both failures are
// host-only diagnostics: the value is the program's own, not the model's.
[[nodiscard]] inline Result<Json> validate_encoded_json(Json&& json) {
  switch (check_json(json.text)) {
  case json_validity::valid:
    return std::move(json);
  case json_validity::too_deep:
    return std::unexpected(Error{
        .message = "the value nests arrays and objects more than " +
                   std::to_string(json_max_depth) + " deep, so it cannot be JSON"});
  case json_validity::invalid:
    break;
  }
  return std::unexpected(Error{.message = "the value holds a string that is not "
                                          "valid UTF-8, so it cannot be JSON"});
}

} // namespace scrydantic::detail
