#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <scrydantic/error.hpp>
#include <string>
#include <string_view>

namespace scrydantic {

/// Serialized JSON boundary value.
///
/// Constructing this value does not parse or validate its text. Each consuming
/// operation validates the JSON and reports its own documented error. Use
/// JsonView to read one, JsonView::to_json() for its canonical form,
/// is_valid_json() to check one without parsing it into a document, and
/// escape_json_string() and format_json_number() for hand-built JSON.
struct Json {
  /// UTF-8 JSON text.
  std::string text{};
};

/// Kind of a JSON value observed through JsonView.
enum class JsonKind : std::uint8_t {
  /// A JSON null, and the kind reported by a default-constructed view.
  null,
  /// A JSON true or false.
  boolean,
  /// A negative whole number that fits a signed 64-bit integer.
  signed_integer,
  /// A non-negative whole number that fits an unsigned 64-bit integer.
  unsigned_integer,
  /// Any other numeric value.
  number,
  /// A JSON string.
  string,
  /// A JSON array.
  array,
  /// A JSON object.
  object,
};

/// Read-only view into a parsed JSON document.
///
/// Parsing produces one immutable document; every view into it shares ownership, so
/// a child view stays valid after the parent is destroyed. Object keys are visited in
/// lexicographic order, matching Scrydantic's canonical form. A default-constructed
/// view reports JsonKind::null and every accessor returns empty.
class JsonView final {
public:
  /// Creates an empty view that reports JsonKind::null.
  JsonView() = default;

  /// Parses JSON text.
  ///
  /// The document holds its own copy of every key and string, so the text need
  /// not outlive the view.
  ///
  /// Text that is not valid JSON fails with an Error whose message and
  /// model_message are the same sentence, saying why and where reading stopped,
  /// such as `JSON text is not valid: expected ':' at line 1, column 7`. Lines
  /// and columns count from 1. A line ends at each line feed, and the column
  /// counts code points, not bytes, so `é` is one column. The position is that
  /// of the first character that cannot be read, or of the end of the text when
  /// it ends too early; for an unterminated string it is the opening quote, and
  /// for a misspelled literal, a number out of range, an invalid escape, or
  /// invalid UTF-8, its first character. The reason is one of `empty input`,
  /// `unexpected end of input`,
  /// `unexpected text after the value`, `expected a value`,
  /// `expected a string key`, `expected ':'`, `expected ',' or ']'`,
  /// `expected ',' or '}'`, `expected a digit`, `number out of range`,
  /// `unterminated string`, `unescaped control character`, `invalid escape`,
  /// `invalid UTF-8`, and `nesting deeper than 256`. The sentence holds nothing
  /// but those words and the position, so a model may see it. The Error has no
  /// issues.
  /// @param text JSON text to parse.
  /// @return The root view, or the Error described above.
  [[nodiscard]] static Result<JsonView> parse(std::string_view text);

  /// Parses the text of a Json, as parse(std::string_view) does.
  /// @param json JSON text to parse.
  /// @return What parse(std::string_view) returns for `json.text`.
  [[nodiscard]] static Result<JsonView> parse(const Json& json);

  /// Reports the kind of the viewed value.
  /// @return The observed JsonKind.
  [[nodiscard]] JsonKind kind() const noexcept;

  /// Reads a boolean value.
  /// @return The boolean, or empty when the value is not a boolean.
  [[nodiscard]] std::optional<bool> boolean() const noexcept;

  /// Reads a signed whole number.
  /// @return The value, or empty unless kind() is JsonKind::signed_integer.
  [[nodiscard]] std::optional<std::int64_t> signed_integer() const noexcept;

  /// Reads an unsigned whole number.
  /// @return The value, or empty unless kind() is JsonKind::unsigned_integer.
  [[nodiscard]] std::optional<std::uint64_t> unsigned_integer() const noexcept;

  /// Reads any numeric value as a double, including integers.
  /// @return The value, or empty when the value is not numeric.
  [[nodiscard]] std::optional<double> number() const noexcept;

  /// Reads a string value.
  /// @return The text, borrowed from the shared document, or empty when the value is
  /// not a string.
  [[nodiscard]] std::optional<std::string_view> string() const noexcept;

  /// Returns the element count of an array or object.
  /// @return The count, and zero for every other kind.
  [[nodiscard]] std::size_t size() const noexcept;

  /// Reads one array element.
  /// @param index Zero-based element index.
  /// @return The element view, or empty when the value is not an array or the index is
  /// out of range.
  [[nodiscard]] std::optional<JsonView> at(std::size_t index) const noexcept;

  /// Reads one object key in lexicographic order.
  /// @param index Zero-based key index.
  /// @return The key, borrowed from the shared document, or empty when the value is not
  /// an object or the index is out of range.
  [[nodiscard]] std::optional<std::string_view>
  key_at(std::size_t index) const noexcept;

  /// Looks up one object member by name.
  /// @param name Member name.
  /// @return The member view, or empty when the value is not an object or has no such
  /// member.
  [[nodiscard]] std::optional<JsonView> find(std::string_view name) const noexcept;

  /// Serializes the viewed value as canonical JSON text.
  ///
  /// The text is Scrydantic's canonical form of the value alone, with object keys in
  /// lexicographic order; it does not reproduce the original spelling. A
  /// default-constructed view serializes as `null`.
  /// @return The value's canonical JSON text.
  [[nodiscard]] Json to_json() const;

private:
  explicit JsonView(std::shared_ptr<const void> value) noexcept;

  // Owns the parsed document and points at this view's node within it.
  std::shared_ptr<const void> value_{};
};

/// Quotes and escapes one JSON string literal, for hosts that assemble small JSON
/// results by hand.
/// @param value Raw UTF-8 text. Non-ASCII bytes pass through unescaped.
/// @return The value as a quoted JSON string with every required escape applied.
[[nodiscard]] std::string escape_json_string(std::string_view value);

/// Spells a double as Scrydantic's canonical JSON spells it, for hosts that
/// assemble small JSON results by hand.
///
/// The text is the one JsonView::to_json() writes for a number held as a double:
/// the shortest digits that read back as the value, positional for decimal
/// exponents from -4 to 15 and otherwise in exponent form with an uppercase `E`,
/// such as `1`, `-0`, `0.0001`, `1E-7`, and `1.5E300`. JSON cannot spell a
/// non-finite number, so infinities and NaN are written as `null`, as JavaScript's
/// JSON.stringify() writes them. Integers need no special form: their decimal
/// digits are already canonical.
///
/// encode() spells a double the same way except for a whole number of magnitude
/// 2^53 or more that std::to_chars writes as plain digits fitting a 64-bit
/// integer: encode() keeps those digits. For `123456789012345680.0` this function
/// writes `1.2345678901234568E17` and encode() writes `123456789012345680`, which
/// reads back as JsonKind::unsigned_integer rather than JsonKind::number.
/// @param value Number to spell.
/// @return The number as JSON text.
[[nodiscard]] std::string format_json_number(double value);

/// Checks that text is exactly one JSON value, without building a document.
///
/// The check is one pass that allocates nothing, and it accepts exactly the text
/// JsonView::parse() accepts: strict RFC 8259 JSON in valid UTF-8, nested at most
/// 256 arrays and objects deep, with nothing but whitespace around the value.
/// Every number must also be representable as a double. One whose magnitude
/// overflows, such as `1e400`, and a nonzero one that rounds to zero, such as
/// `1e-400`, are rejected even though the grammar allows them. The smallest
/// subnormal, `5e-324`, and a zero with any exponent are accepted.
/// Use it to check a Json before storing or forwarding it when nothing needs to
/// read the value.
/// @param text Text to check.
/// @return Whether JsonView::parse() would accept the text.
[[nodiscard]] bool is_valid_json(std::string_view text) noexcept;

} // namespace scrydantic
