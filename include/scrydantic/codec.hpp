#pragma once

#if !defined(__cpp_impl_reflection)
#error "Scrydantic requires a compiler implementing P2996 (GCC 16.2 or newer)"
#endif

#include <array>
#include <cstddef>
#include <scrydantic/annotations.hpp>
#include <scrydantic/detail/decode.hpp>
#include <scrydantic/detail/encode.hpp>
#include <scrydantic/detail/json_text.hpp>
#include <scrydantic/detail/meta.hpp>
#include <scrydantic/detail/model.hpp>
#include <scrydantic/detail/schema.hpp>
#include <scrydantic/error.hpp>
#include <scrydantic/json.hpp>
#include <string>
#include <string_view>
#include <type_traits>

// The wire names of a reflected type, as keys_v and tags_v list them. Each
// checks the type as encode() does, so a type outside Encodable fails with the
// same diagnostic, and then reads the names the codec itself writes.
namespace scrydantic::detail {

template <typename Type> consteval std::string_view keys_diagnostic() {
  if constexpr (kind_of(^^Type) != value_kind::aggregate) {
    return static_string(display(^^Type) +
                         " is not a plain aggregate, so it has no JSON object keys");
  } else {
    return encodable_diagnostic<Type>();
  }
}

template <typename Type> consteval auto object_keys() {
  constexpr std::string_view problem = keys_diagnostic<Type>();
  static_assert(problem.empty(), problem);
  if constexpr (!problem.empty()) {
    return std::array<std::string_view, 0>{};
  } else {
    constexpr auto fields = fields_v<Type>;
    std::array<std::string_view, fields.size()> keys{};
    for (std::size_t index = 0; index < fields.size(); ++index) {
      keys[index] = fields[index].key_view();
    }
    return keys;
  }
}

template <typename Type> consteval std::string_view tags_diagnostic() {
  if constexpr (kind_of(^^Type) != value_kind::variant) {
    return static_string(display(^^Type) + " is not a std::variant, so it has no tags");
  } else {
    return encodable_diagnostic<Type>();
  }
}

template <typename Type> consteval auto variant_tags() {
  constexpr std::string_view problem = tags_diagnostic<Type>();
  static_assert(problem.empty(), problem);
  if constexpr (!problem.empty()) {
    return std::array<std::string_view, 0>{};
  } else {
    constexpr auto alternatives = alternatives_v<Type>;
    std::array<std::string_view, alternatives.size()> tags{};
    for (std::size_t index = 0; index < alternatives.size(); ++index) {
      tags[index] = *tag_of(alternatives[index]);
    }
    return tags;
  }
}

} // namespace scrydantic::detail

/// Scrydantic's reflected codec, built on P2996: strict encoding and decoding of
/// reflected values, and the JSON Schema generated for them (schema_v and
/// input_schema_v).
namespace scrydantic {

/// Encodes a reflected value as Scrydantic's canonical JSON.
///
/// Every number and every spliced `Json` payload is written in canonical form, so
/// the text is the one JsonView::to_json() writes for it. Canonical form reads
/// each number in a payload as a 64-bit integer or a double, so a number that
/// neither holds exactly loses digits; encode_verbatim() keeps a payload's own
/// text. The returned text is canonical within the current Scrydantic version but
/// is not a versioned archival format. The value is the program's own, so its
/// constraint annotations and validators are not checked. A type outside
/// Encodable fails to compile with the offending member path and the reason.
/// @tparam Type Value from the Encodable family.
/// @param value Value to encode without modifying it.
/// @return Canonical JSON, or an Error whose `message` names the path of a
/// non-finite number, an undeclared enumerator value, an invalid Json, or a
/// valueless variant or std::indirect, or says that a string is not valid UTF-8
/// or that the value nests arrays and objects deeper than JsonView::parse
/// accepts, and whose `model_message` is empty.
template <typename Type> [[nodiscard]] Result<Json> encode(const Type& value) {
  constexpr std::string_view problem = detail::encodable_diagnostic<Type>();
  static_assert(problem.empty(), problem);
  if constexpr (!problem.empty()) {
    return {};
  } else {
    return detail::encode_value<Type, detail::spelling::canonical>(value).and_then(
        detail::validate_encoded_json);
  }
}

/// Encodes a reflected value as JSON, keeping spliced `Json` payloads byte for byte.
///
/// The writer is the one encode() uses, with two parts of the text left as they
/// are instead of canonicalized. A `Json` payload is checked and then copied
/// exactly, with its own whitespace, key order, and number text, so a stored
/// document or a number wider than a double reaches the output unchanged. A
/// float or double is written as std::to_chars' shortest text, such as `1e-07`
/// where encode() writes `1E-7`. Keys are still in canonical order, strings still
/// use the canonical escapes, and the whole text is checked as encode()'s is. As
/// with encode(), constraint annotations and validators are not checked.
/// Reflected tool handlers encode their results this way.
///
/// The check reads this function's own output, so encode() succeeding does not
/// imply that this does. A payload with a duplicate key keeps both members here,
/// where encode() keeps only the last, so a deeply nested earlier value can take
/// this output past the nesting limit when encode()'s is within it.
/// @tparam Type Value from the Encodable family.
/// @param value Value to encode without modifying it.
/// @return JSON text, or an Error of the kinds encode() reports, for this
/// function's output.
template <typename Type> [[nodiscard]] Result<Json> encode_verbatim(const Type& value) {
  constexpr std::string_view problem = detail::encodable_diagnostic<Type>();
  static_assert(problem.empty(), problem);
  if constexpr (!problem.empty()) {
    return {};
  } else {
    return detail::encode_value<Type, detail::spelling::verbatim>(value).and_then(
        detail::validate_encoded_json);
  }
}

/// Decodes a parsed JSON value into a reflected value, strictly.
///
/// The rules are the ones reflected tool arguments are decoded by. Each member
/// that the input supplies must also meet its constraint annotations, and each
/// value that check annotations name a validator for must pass it. Decoding
/// goes on past a failure, so one call reports every issue it finds, up to
/// max_issues. A failure is an Error whose `issues` lists them, each with its
/// kind, its path, and what the type required there, and whose `message` and
/// `model_message` are both one `<path> <reason>` line per issue, such as
/// `$.shapes[0].radius must be a number`, except that in `message` the line of a
/// failed check gives the validator's own `message`, where it has one. A type
/// outside Decodable fails to compile with the offending member path and the
/// reason.
/// @tparam Type Cv-unqualified value type from the Decodable family.
/// @param view Parsed JSON value to decode.
/// @return The decoded value, or every issue that kept it from decoding.
template <typename Type> [[nodiscard]] Result<Type> decode(const JsonView& view) {
  constexpr std::string_view problem = detail::decodable_diagnostic<Type>();
  static_assert(problem.empty(), problem);
  if constexpr (!problem.empty()) {
    // Built with std::unexpect, as detail::decode_failed() explains, so that a
    // rejected type of that shape prints the static_assert and nothing more.
    return Result<Type>{std::unexpect};
  } else {
    return detail::decode<Type>(view);
  }
}

/// Parses JSON text and decodes it into a reflected value, strictly.
///
/// A string literal or a std::string can be passed as it is. Text that is not
/// JSON fails with one IssueKind::invalid_json issue at `$`, whose reason says
/// why and where reading stopped, such as
/// `is not valid JSON: expected ':' at line 1, column 7`, and with
/// `JSON text is not valid JSON: expected ':' at line 1, column 7` as both
/// messages. The column counts code points, not bytes; JsonView::parse() lists
/// the reasons. Every other failure is reported as decode(const JsonView&)
/// reports it.
/// @tparam Type Cv-unqualified value type from the Decodable family.
/// @param text JSON text to parse and decode.
/// @return The decoded value, or the parse or decode failure.
template <typename Type>
[[nodiscard]] Result<Type> decode(const std::string_view text) {
  // Composed from std::expected's monadic operations, which build their failure
  // with std::unexpect: see detail::decode_failed(). Text that is not JSON is
  // read once more to describe it, which only a failure pays for.
  return JsonView::parse(text)
      .transform_error([text](const Error&) {
        return detail::invalid_json_error("JSON text is not valid JSON",
                                          detail::describe_invalid_json(text));
      })
      .and_then([](const JsonView& view) { return decode<Type>(view); });
}

/// Parses the text of a Json and decodes it, as decode(std::string_view) does.
/// @tparam Type Cv-unqualified value type from the Decodable family.
/// @param json JSON text to parse and decode.
/// @return What decode(std::string_view) returns for `json.text`.
template <typename Type> [[nodiscard]] Result<Type> decode(const Json& json) {
  return decode<Type>(std::string_view{json.text});
}

/// The JSON keys of a reflected object type, in the order encode() writes them.
///
/// Each key is the member's scrydantic::name annotation or else its identifier,
/// the key that encoding, decoding, schemas, and failure paths use. The keys are
/// in lexical order, which is also the order JsonView::key_at() visits a parsed
/// object in. Every declared member is listed, also one that decoding treats as
/// optional. A tagged alternative also carries its tag under `"type"` when it is
/// encoded in a variant; that key is not a member, so it is not listed. A type
/// that is not an Encodable plain aggregate fails to compile with the reason.
///
/// Usage: `for (const std::string_view key : scrydantic::keys_v<Request>)`, or
/// `template for` to visit the keys at compile time.
/// @tparam Type Plain aggregate from the Encodable family.
template <typename Type>
inline constexpr auto keys_v = detail::object_keys<std::remove_cvref_t<Type>>();

/// The tags of a tagged variant's alternatives, in alternative order.
///
/// `tags_v<Variant>[index]` is the scrydantic::tag of
/// `std::variant_alternative_t<index, Variant>`: the value that the encoded
/// object carries under `"type"`, and that decoding selects the alternative by.
/// A host that receives the tag apart from the object, such as the event name
/// of a server-sent event, can match it here before it decodes. A type that is
/// not an Encodable variant fails to compile with the reason.
/// @tparam Variant std::variant from the Encodable family.
template <typename Variant>
inline constexpr auto tags_v = detail::variant_tags<std::remove_cvref_t<Variant>>();

} // namespace scrydantic
