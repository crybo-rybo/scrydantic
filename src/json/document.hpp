#pragma once

#include <cstddef>
#include <cstdint>
#include <expected>
#include <optional>
#include <scrydantic/detail/json_text.hpp>
#include <scrydantic/json.hpp>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

// Scrydantic's own strict JSON layer: an immutable document tree, a single-pass
// parser, an allocation-free validator that accepts exactly what the parser
// accepts, and the canonical writer.
//
// What the parser accepts is RFC 8259 JSON text holding exactly one value, with
// these decisions where the RFC leaves room:
//
// - Nesting: at most max_depth arrays and objects open at once, counting an
//   empty one. Deeper input is rejected, which bounds the parser's and the
//   tree's recursion.
// - Whitespace: only space, tab, line feed, and carriage return, around any
//   token. Input with no value at all, whitespace only included, is rejected,
//   as are a byte order mark, a NUL, and anything after the value.
// - Strings: UTF-8 is validated strictly, in keys and values: no overlong
//   forms, no encoded surrogates, nothing above U+10FFFF, no truncated
//   sequence. Control characters below 0x20 must be escaped. A \u escape of a
//   surrogate must be a high surrogate followed at once by a \u escape of a
//   low one; a lone or reversed surrogate is rejected. \u0000 is accepted and
//   decodes to a NUL byte.
// - Numbers: a number spelled as an optional '-' and digits alone is an
//   integer: unsigned_integer when not negative and within uint64,
//   signed_integer when negative and within int64. "-0" is not negative, and
//   is the number -0.0 so its sign survives a canonical round trip. Every
//   other number, including one with a fraction or an exponent even when its
//   value is whole (1.0, 1e2), and an integer outside those ranges, is a
//   double, correctly rounded. A number whose magnitude rounds to infinity,
//   or a nonzero one that rounds to zero, is rejected.
// - Duplicate object keys: the last occurrence wins, at every level.
//
// Canonical text has no insignificant whitespace, object keys in lexical byte
// order, strings with only the escapes JSON requires (\" \\ \b \f \n \r \t,
// and \u00XX with uppercase hex for any other control character; '/' and
// everything from 0x7F up are written as they are), and numbers as described
// by append_double in json/number.hpp.

namespace scrydantic::detail::json {

// The deepest nesting the parser accepts.
inline constexpr std::size_t max_depth = json_max_depth;

class Value;

using Array = std::vector<Value>;
using Member = std::pair<std::string, Value>;
// Sorted by key in lexical byte order, keys unique.
using Object = std::vector<Member>;

// One immutable JSON value. Only the parser builds one with content; a
// default-constructed value is null. Objects are sorted vectors, so lookup is
// a binary search and ordered iteration is the canonical order. Destruction
// recurses once per nesting level, which max_depth bounds.
class Value final {
public:
  Value() noexcept = default;

  [[nodiscard]] JsonKind kind() const noexcept;

  [[nodiscard]] bool is_null() const noexcept;

  [[nodiscard]] std::optional<bool> boolean() const noexcept;

  // Only for kind() == JsonKind::signed_integer.
  [[nodiscard]] std::optional<std::int64_t> signed_integer() const noexcept;

  // Only for kind() == JsonKind::unsigned_integer.
  [[nodiscard]] std::optional<std::uint64_t> unsigned_integer() const noexcept;

  // Any numeric kind, as a double; an integer converts to the nearest double.
  [[nodiscard]] std::optional<double> number() const noexcept;

  // Borrowed from this value.
  [[nodiscard]] std::optional<std::string_view> string() const noexcept;

  [[nodiscard]] const Array* array() const noexcept;

  [[nodiscard]] const Object* object() const noexcept;

  // The element count of an array or member count of an object; zero otherwise.
  [[nodiscard]] std::size_t size() const noexcept;

  // An object member's value, found by binary search; null when this is not an
  // object or has no such key.
  [[nodiscard]] const Value* find(std::string_view key) const noexcept;

private:
  friend class Builder;

  // Alternatives in JsonKind order, so kind() is the index.
  using Data = std::variant<std::nullptr_t, bool, std::int64_t, std::uint64_t, double,
                            std::string, Array, Object>;

  explicit Value(Data data) noexcept : data_(std::move(data)) {}

  Data data_{};
};

// Why the reader rejected a text: the first rule it found broken, reading from
// the start. reason() gives each its phrase, written for a model to act on.
enum class Fault : std::uint8_t {
  // Nothing but whitespace.
  empty_input,
  // The text ends inside a value, outside a string.
  end_of_input,
  // A value is followed by something other than whitespace.
  text_after_value,
  // Where a value must start, a byte that starts none, or a literal other
  // than true, false, or null.
  expected_value,
  // Where an object member must start, something other than a string.
  expected_key,
  expected_colon,
  expected_comma_or_bracket,
  expected_comma_or_brace,
  // A number's '-', '.', or exponent not followed by a digit.
  expected_digit,
  // A number whose magnitude rounds to infinity, or a nonzero one that rounds
  // to zero, as a double.
  number_out_of_range,
  // The text ends inside a string, an escape or a UTF-8 sequence included.
  unterminated_string,
  // A byte below 0x20 inside a string.
  control_character,
  // A backslash followed by anything but a JSON escape, a \u escape with a
  // non-hex digit, or a surrogate escape that is not a high one followed at
  // once by a low one.
  invalid_escape,
  invalid_utf8,
  // The container that opens one level past max_depth.
  too_deep,
};

// Where the reader stopped in a text it rejected, and why. The offset is that
// of the first byte the reader could not accept, or the text's size when the
// text ends before the document does. A fault in a token points at the token's
// first byte instead: the opening quote of an unterminated string, the
// backslash of an invalid escape (the high surrogate's, for a broken pair), the
// lead byte of invalid UTF-8, and the first byte of a number out of range or of
// a literal that is none of true, false, and null. Every byte before the offset
// was read and accepted, so it is valid UTF-8.
struct Rejection {
  Fault fault{};
  std::size_t offset{};

  [[nodiscard]] bool operator==(const Rejection&) const = default;
};

// Parses exactly one JSON document, or says where and why the rules above
// reject it.
[[nodiscard]] std::expected<Value, Rejection> parse(std::string_view text);

// True exactly when parse(text) would succeed, without allocating.
[[nodiscard]] bool validate(std::string_view text) noexcept;

// What parse(text) would reject, without allocating: the same rejection, or
// empty when it would succeed.
[[nodiscard]] std::optional<Rejection> diagnose(std::string_view text) noexcept;

using Validity = json_validity;

// What validate(text) decides, and when it rejects the text, whether reading
// stopped at a container nested deeper than max_depth.
[[nodiscard]] Validity check(std::string_view text) noexcept;

// The phrase for a fault, such as `expected ':'` or `unterminated string`.
[[nodiscard]] std::string_view reason(Fault fault) noexcept;

// A position in text, both counted from 1. A line ends at each line feed, so a
// carriage return before one is the last column of its line. The column counts
// code points, not bytes, because a reader of the text, a model or a person,
// counts characters rather than the bytes UTF-8 spends on them. A tab is one
// column.
struct Position {
  std::size_t line{};
  std::size_t column{};

  [[nodiscard]] bool operator==(const Position&) const = default;
};

// The position of byte `offset` of `text`, or of its end when `offset` is its
// size. Code points are counted as the bytes that are not UTF-8 continuation
// bytes, which is exact for the valid UTF-8 that precedes a rejection.
[[nodiscard]] Position locate(std::string_view text, std::size_t offset) noexcept;

// A rejection of `text` as one phrase: `<reason> at line <line>, column
// <column>`, such as `expected ':' at line 1, column 7`.
[[nodiscard]] std::string describe(std::string_view text, Rejection rejection);

// Appends the canonical text of `value`.
void write(const Value& value, std::string& output);

[[nodiscard]] std::string write(const Value& value);

// Appends `text` as a canonical JSON string literal, quotes included. Bytes
// from 0x7F up pass through unchanged, so valid UTF-8 in is valid UTF-8 out.
void append_quoted(std::string& output, std::string_view text);

[[nodiscard]] std::string quote(std::string_view text);

} // namespace scrydantic::detail::json
