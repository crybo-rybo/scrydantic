#pragma once

#if !defined(__cpp_impl_reflection)
#error "Scrydantic requires a compiler implementing P2996 (GCC 16.2 or newer)"
#endif

#include <meta>
#include <scrydantic/annotations.hpp>
#include <scrydantic/decode_options.hpp>
#include <scrydantic/detail/decode.hpp>
#include <scrydantic/detail/encode.hpp>
#include <scrydantic/detail/json_text.hpp>
#include <scrydantic/detail/meta.hpp>
#include <scrydantic/detail/schema.hpp>
#include <scrydantic/detail/validate.hpp>
#include <scrydantic/error.hpp>
#include <scrydantic/json.hpp>
#include <string_view>
#include <type_traits>

/// Scrydantic's reflected codec, built on P2996: strict encoding and decoding of
/// reflected values, and the JSON Schema generated for them (schema_v and
/// input_schema_v).
namespace scrydantic {

/// Values supported by reflected schema generation and strict marshalling.
///
/// Supported shapes are a deliberately closed matrix: booleans, bounded
/// non-character integers, finite floats, strings, scoped enums, `Json`, one
/// optional layer, vectors except `vector<bool>`, fixed arrays, `std::map` and
/// `std::unordered_map` keyed by `std::string`, `std::indirect`, plain aggregates
/// of supported members, and variants whose alternatives a JSON value's kind
/// tells apart. A `Json` is any JSON value: its schema is
/// the empty schema `{}`, which accepts every value, decoding captures the
/// canonical text of whatever value sits at its position, `null` included, and
/// encoding checks that text and splices it, so a document can carry an opaque
/// payload through a typed value. An indirect encodes, decodes, and has the
/// schema of the value it owns, and `std::optional<std::indirect<T>>` is its
/// nullable form. A map is the JSON object whose keys are data: its schema allows
/// any key, encoding writes its keys in canonical order whatever its comparator
/// or hash, and decoding makes an entry of every member. An aggregate may contain
/// itself, through `std::vector`, a map, or `std::indirect`, when every such
/// cycle passes through a `std::optional`, a `std::vector`, a map, or a variant
/// alternative after the first, so that default-constructing it ends; its schema
/// refers to it with `$ref`, under a name spelled from its type. Every supported
/// value has a generated schema, encodes, and decodes. A member's constraint
/// annotations must apply to its type and admit a value, in this family and in
/// the others. Reflected entry points report the member path and the reason a
/// type falls outside this family.
///
/// A variant holds at most one alternative of each JSON kind: null
/// (`std::monostate`), boolean, integer, number (`float`, `double`), string
/// (`std::string` or a scoped enum), array, and object (an aggregate or a map).
/// An integer and a floating-point alternative may stand together: a JSON
/// integer within the integer alternative's range is that alternative, and any
/// other number the floating-point one. Several aggregates may stand together
/// when each carries a distinct scrydantic::tag, which their objects are told
/// apart by; a variant of such aggregates alone is the tagged union. A variant's
/// schema is the `anyOf` of its alternatives' schemas, in alternative order.
/// Encoding writes the alternative held, and decoding reads the one that the
/// value's kind selects, so the issues of a value are that alternative's own. A
/// `Json`, a `std::optional`, a `std::indirect`, and another variant have no one
/// kind and are no alternative, and a `std::optional` holds no variant with a
/// `std::monostate`, since null would then be both.
template <typename Type>
concept SupportedValue =
    detail::value_problem(^^std::remove_cvref_t<Type>, detail::value_family::supported)
        .empty();

/// Values scrydantic::encode() accepts: SupportedValue plus what encoding can
/// read but decoding cannot fill.
///
/// `std::string_view` is the one encode-only leaf: it is written as a JSON
/// string, for text the caller owns elsewhere, and it has no schema. Aggregates
/// here need not be default-constructible, movable, or free of `const` members,
/// and may borrow a member through a reference, since encoding only reads them;
/// for the same reason, a cycle may pass through any variant alternative.
/// Encoding writes no schema, so a recursive type needs no definition name.
template <typename Type>
concept Encodable =
    detail::value_problem(^^std::remove_cvref_t<Type>, detail::value_family::encodable)
        .empty();

/// Values scrydantic::decode() produces: the shapes of SupportedValue.
///
/// Decoding writes no schema, so a recursive type whose schema could not name
/// its definitions, and which is therefore not a SupportedValue, is still
/// Decodable. Decoding produces a value, so the target itself must be a
/// cv-unqualified object type, as scrydantic::decode() requires.
template <typename Type>
concept Decodable =
    !detail::is_qualified(^^Type) &&
    detail::value_problem(^^Type, detail::value_family::decodable).empty();

/// Complete plain aggregates accepted as reflected tool arguments.
template <typename Type>
concept ToolArguments = detail::tool_arguments_problem(^^Type).empty();

/// Canonical JSON Schema generated for any supported reflected value.
///
/// A tool's result type is a supported root too: bind() and describe() publish
/// its schema as the tool's `output_schema`, beside the `input_schema` of its
/// arguments. A member's constraint annotations add their keywords to its schema,
/// and decoding checks every one of them but `pattern`. A member with a default
/// member initializer states it as `default`, spelled as encode() spells it,
/// wherever the default-constructed class is a constant expression and the value
/// is one whose spelling is known at compile time; elsewhere the keyword is left
/// out. A scrydantic::title, keyword, or keyword_string adds its keyword to the
/// schema and does nothing else. A type outside
/// SupportedValue fails to compile with the offending member path and the reason.
/// @tparam Value Type satisfying SupportedValue.
template <typename Value>
inline constexpr std::string_view schema_v = detail::checked_schema<Value>();

/// Canonical JSON Schema generated for a reflected argument aggregate.
/// @tparam Args Type satisfying ToolArguments.
template <typename Args>
inline constexpr std::string_view input_schema_v = detail::checked_input_schema<Args>();

/// Encodes a reflected value as Scrydantic's canonical JSON.
///
/// Every number and every spliced `Json` payload is written in canonical form, so
/// the text is the one JsonView::to_json() writes for it. Canonical form reads
/// each number in a payload as a 64-bit integer or a double, so a number that
/// neither holds exactly loses digits; encode_verbatim() keeps a payload's own
/// text. The returned text is canonical within the current Scrydantic version but
/// is not a versioned archival format. The value is the program's own, so its
/// constraint annotations and validators are not checked; validate() checks
/// them. A type outside Encodable fails to compile with the offending member
/// path and the reason.
/// @tparam Type Value from the Encodable family.
/// @param value Value to encode without modifying it.
/// @return Canonical JSON, or an Error whose `message` names the path of a
/// non-finite number, an undeclared enumerator value, an invalid Json, or a
/// valueless variant or std::indirect, or says that a string or a map's key is
/// not valid UTF-8 or that the value nests arrays and objects deeper than
/// JsonView::parse accepts, and whose `model_message` is empty. Encoding stops
/// at the first such failure, in the order the text is written.
template <typename Type> [[nodiscard]] Result<Json> encode(const Type& value) {
  constexpr std::string_view problem = detail::encodable_diagnostic<Type>();
  static_assert(problem.empty(), problem);
  if constexpr (!problem.empty()) {
    return {};
  } else {
    return detail::encode_value<Type, detail::spelling::canonical>(value);
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
/// use the canonical escapes, and the text is valid JSON as encode()'s is. As
/// with encode(), constraint annotations and validators are not checked.
/// Reflected tool handlers encode their results this way.
///
/// The nesting limit applies to the text each function writes, so encode()
/// succeeding does not imply that this does. A payload with a duplicate key
/// keeps both members here, where encode() keeps only the last, so a deeply
/// nested earlier value can take this output past the nesting limit when
/// encode()'s is within it.
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
    return detail::encode_value<Type, detail::spelling::verbatim>(value);
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
///
/// `options` let a JSON string stand for the value its text spells, where the
/// type requires a value of another kind; DecodeOptions says which strings, and
/// what is never converted. Every option is off unless it is set, so a call
/// without them is the strict decode. A value that a string stands for is held
/// to every rule above, and fails with the same kinds, paths, and reasons.
/// @tparam Type Cv-unqualified value type from the Decodable family.
/// @param view Parsed JSON value to decode.
/// @param options What to accept beyond the strict rules; nothing by default.
/// @return The decoded value, or every issue that kept it from decoding.
template <typename Type>
[[nodiscard]] Result<Type> decode(const JsonView& view,
                                  const DecodeOptions& options = {}) {
  constexpr std::string_view problem = detail::decodable_diagnostic<Type>();
  static_assert(problem.empty(), problem);
  if constexpr (!problem.empty()) {
    // Built with std::unexpect, as detail::decode_failed() explains, so that a
    // rejected type of that shape prints the static_assert and nothing more.
    return Result<Type>{std::unexpect};
  } else {
    return detail::decode<Type>(view, options);
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
/// reports it. The text itself must be JSON whatever `options` hold: they apply
/// to the values in it.
/// @tparam Type Cv-unqualified value type from the Decodable family.
/// @param text JSON text to parse and decode.
/// @param options What to accept beyond the strict rules; nothing by default.
/// @return The decoded value, or the parse or decode failure.
template <typename Type>
[[nodiscard]] Result<Type> decode(const std::string_view text,
                                  const DecodeOptions& options = {}) {
  // Composed from std::expected's monadic operations, which build their failure
  // with std::unexpect: see detail::decode_failed(). Text that is not JSON is
  // read once more to describe it, which only a failure pays for.
  return JsonView::parse(text)
      .transform_error([text](const Error&) {
        return detail::invalid_json_error("JSON text is not valid JSON",
                                          detail::describe_invalid_json(text));
      })
      .and_then(
          [&options](const JsonView& view) { return decode<Type>(view, options); });
}

/// Parses the text of a Json and decodes it, as decode(std::string_view) does.
/// @tparam Type Cv-unqualified value type from the Decodable family.
/// @param json JSON text to parse and decode.
/// @param options What to accept beyond the strict rules; nothing by default.
/// @return What decode(std::string_view) returns for `json.text`.
template <typename Type>
[[nodiscard]] Result<Type> decode(const Json& json, const DecodeOptions& options = {}) {
  return decode<Type>(std::string_view{json.text}, options);
}

/// Checks a value the program built against the constraints and validators of
/// its type.
///
/// Decoding checks them on what it reads, and nothing checks a value that was
/// filled some other way: from a database row, from a configuration file, or by
/// the program's own code. This checks `value` against every constraint
/// annotation and validator that decoding it would have checked. It walks the
/// value as decoding walks its JSON, through `std::optional`, `std::indirect`,
/// vectors, arrays, maps, variants, nested aggregates, and flattened members,
/// and reports each issue with the kind (IssueKind::violated_constraint or
/// IssueKind::failed_check), the path, and the reason that decoding would give
/// it, in the same order: a map's entries in key order, whatever order the map
/// iterates in. A member's validators run once it meets its constraints, and a
/// class's once every member of the object is valid. It stops past max_issues,
/// as decoding does.
///
/// Two things differ from a decode of the same value's JSON, and both follow
/// from there being no JSON:
/// - Every member is checked. Decoding leaves a member that its input lacks at
///   its default, unchecked; here a default is a value like any other. A
///   disengaged `std::optional` has no value to check.
/// - A `float` or `double` is checked once, as the value it is. Decoding checks
///   a number twice, as its text spelled it and as converted, and has a reason
///   of its own for one that only the conversion carries past a bound; a value
///   past a bound is reported here with the bound's plain reason.
///
/// Only constraints and validators are checked. `pattern` is not, as decoding
/// does not check it. What encode() refuses is left to encode(), and none of it
/// is an issue here: a number that is not finite, an enumerator value that is
/// not declared, a string that is not UTF-8, an invalid `Json`, a valueless
/// variant or `std::indirect`, and whatever nests more than max_json_depth
/// arrays and objects deep, which is not walked. A NaN meets no `range` and no
/// `multiple_of`, though, and is reported where it stands under one, as an
/// infinity is that is past a bound or under a `multiple_of`.
///
/// The function only reads `value`, so it takes what encode() takes. A type with
/// no constraint and no validator anywhere in it is known at compile time to be
/// valid, and the call does nothing. A type outside Encodable fails to compile
/// with the offending member path and the reason.
/// @tparam Type Value from the Encodable family.
/// @param value Value to check without modifying it.
/// @return Success, or an Error that lists every violation as decode() lists
/// them: in `issues`, and as one `<path> <reason>` line each in `message` and
/// `model_message`, such as `$.v must be at most 10`.
template <typename Type> [[nodiscard]] Status validate(const Type& value) {
  constexpr std::string_view problem = detail::encodable_diagnostic<Type>();
  static_assert(problem.empty(), problem);
  if constexpr (!problem.empty()) {
    return {};
  } else {
    return detail::validate<std::remove_cvref_t<Type>>(value);
  }
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
/// of a server-sent event, can match it here before it decodes. The list pairs
/// a tag with every alternative, so a variant with an alternative that carries
/// none, which a JSON kind selects instead, has no `tags_v`. A type that is not
/// an Encodable variant of tagged aggregates fails to compile with the reason.
/// @tparam Variant std::variant of tagged aggregates from the Encodable family.
template <typename Variant>
inline constexpr auto tags_v = detail::variant_tags<std::remove_cvref_t<Variant>>();

} // namespace scrydantic
