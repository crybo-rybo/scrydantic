#pragma once

#if !defined(__cpp_impl_reflection)
#error "Scrydantic requires a compiler implementing P2996 (GCC 16 or newer)"
#endif

#include <scrydantic/annotations.hpp>
#include <scrydantic/detail/decode.hpp>
#include <scrydantic/detail/encode.hpp>
#include <scrydantic/detail/meta.hpp>
#include <scrydantic/detail/schema.hpp>
#include <scrydantic/error.hpp>
#include <scrydantic/json.hpp>
#include <string>
#include <string_view>

/// Scrydantic's reflected codec, built on P2996: strict encoding and decoding of
/// reflected values, and the JSON Schema generated for them (schema_v and
/// input_schema_v).
namespace scrydantic {

/// Encodes a reflected value as Scrydantic's canonical JSON.
///
/// This is the same value encoder used for reflected tool-handler results. The
/// returned text is canonical within the current Scrydantic version but is not a
/// versioned archival format. A type outside Encodable fails to compile with the
/// offending member path and the reason.
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

/// Decodes a parsed JSON value into a reflected value, strictly.
///
/// The rules are the ones reflected tool arguments are decoded by. Decoding goes
/// on past a failure, so one call reports every issue it finds, up to
/// max_issues. A failure is an Error whose `issues` lists them, each with its
/// kind, its path, and what the type required there, and whose `message` and
/// `model_message` are both one `<path> <reason>` line per issue, such as
/// `$.shapes[0].radius must be a number`. A type outside Decodable fails to
/// compile with the offending member path and the reason.
/// @tparam Type Cv-unqualified value type from the Decodable family.
/// @param view Parsed JSON value to decode.
/// @return The decoded value, or every issue that kept it from decoding.
template <typename Type> [[nodiscard]] Result<Type> decode(const JsonView& view) {
  constexpr std::string_view problem = detail::decodable_diagnostic<Type>();
  static_assert(problem.empty(), problem);
  if constexpr (!problem.empty()) {
    return std::unexpected(Error{});
  } else {
    return detail::decode<Type>(view);
  }
}

/// Parses JSON text and decodes it into a reflected value, strictly.
///
/// Text that is not JSON fails with `JSON text is not valid JSON` as both
/// messages, and one IssueKind::invalid_json issue at `$`; every other failure
/// is reported as decode(const JsonView&) reports it.
/// @tparam Type Cv-unqualified value type from the Decodable family.
/// @param json JSON text to parse and decode.
/// @return The decoded value, or the parse or decode failure.
template <typename Type> [[nodiscard]] Result<Type> decode(const Json& json) {
  auto parsed = JsonView::parse(json);
  if (!parsed) {
    return std::unexpected(detail::invalid_json_error("JSON text is not valid JSON"));
  }
  return decode<Type>(*parsed);
}

} // namespace scrydantic
