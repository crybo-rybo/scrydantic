#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <scrydantic/json.hpp>
#include <string>
#include <string_view>

// What the header-only reflected codec calls across the compiled boundary of the
// JSON core, besides the public API: its text operations, a borrowed handle on a
// parsed value, and the one definition of well-formed UTF-8 that the core and the
// codec share. None of it is part of the stable API. The document type behind it
// stays inside src/json/document.hpp, which states what is accepted and how
// canonical text is written; outside the core and the codec, parsed JSON is read
// through the public JsonView.
namespace scrydantic::detail {

// The deepest nesting of arrays and objects the parser accepts, counting an
// empty one.
inline constexpr std::size_t json_max_depth = 256;

// A UTF-8 lead byte's sequence length and the range its second byte must fall
// in (Unicode Table 3-7, "Well-Formed UTF-8 Byte Sequences"). Length zero marks
// a byte that cannot start a sequence: a continuation byte, an overlong lead
// (C0, C1), or one beyond U+10FFFF (F5 and up).
struct utf8_lead {
  std::size_t length{};
  unsigned int low{};
  unsigned int high{};
};

// The sequence that `byte`, which is not ASCII, starts.
[[nodiscard]] constexpr utf8_lead utf8_lead_of(const unsigned char byte) noexcept {
  if (byte < 0xC2U) {
    return {};
  }
  if (byte <= 0xDFU) {
    return {.length = 2, .low = 0x80U, .high = 0xBFU};
  }
  if (byte <= 0xEFU) {
    // E0 would be overlong below A0; ED above 9F encodes a surrogate.
    return {.length = 3,
            .low = byte == 0xE0U ? 0xA0U : 0x80U,
            .high = byte == 0xEDU ? 0x9FU : 0xBFU};
  }
  if (byte <= 0xF4U) {
    // F0 would be overlong below 90; F4 above 8F is past U+10FFFF.
    return {.length = 4,
            .low = byte == 0xF0U ? 0x90U : 0x80U,
            .high = byte == 0xF4U ? 0x8FU : 0xBFU};
  }
  return {};
}

// Whether the first `length` bytes of `sequence`, its lead byte among them,
// follow the lead as `expected` requires.
[[nodiscard]] constexpr bool utf8_follows(const char* const sequence,
                                          const utf8_lead& expected,
                                          const std::size_t length) noexcept {
  if (length < 2) {
    return true;
  }
  const auto second = static_cast<unsigned char>(sequence[1]);
  if (second < expected.low || second > expected.high) {
    return false;
  }
  for (std::size_t index = 2; index < length; ++index) {
    const auto continuation = static_cast<unsigned char>(sequence[index]);
    if (continuation < 0x80U || continuation > 0xBFU) {
      return false;
    }
  }
  return true;
}

// The length of the well-formed UTF-8 sequence that starts `text`, whose first
// byte is not ASCII, or zero when `text` starts with none.
[[nodiscard]] constexpr std::size_t
utf8_sequence_length(const std::string_view text) noexcept {
  const auto expected = utf8_lead_of(static_cast<unsigned char>(text.front()));
  if (expected.length == 0 || text.size() < expected.length ||
      !utf8_follows(text.data(), expected, expected.length)) {
    return 0;
  }
  return expected.length;
}

// Whether `text` is well-formed UTF-8: what a JSON string may hold. Usable at
// compile time, for the keys, tags, and enumerator names the codec writes.
[[nodiscard]] constexpr bool is_utf8(std::string_view text) noexcept {
  while (!text.empty()) {
    std::size_t length = 1;
    if (static_cast<unsigned char>(text.front()) >= 0x80U) {
      length = utf8_sequence_length(text);
      if (length == 0) {
        return false;
      }
    }
    text.remove_prefix(length);
  }
  return true;
}

// What check_json() found: valid JSON, text that stopped being read at a
// container nested deeper than json_max_depth, or text that breaks another rule.
enum class json_validity : std::uint8_t {
  valid,
  too_deep,
  invalid,
};

// is_valid_json() that also says why it rejects text, in the same single pass,
// for text that is to stand inside `depth` open arrays and objects: they count
// toward json_max_depth. Reading stops at the first rule broken, so text found
// too deep may break another rule further on.
[[nodiscard]] json_validity check_json(std::string_view input,
                                       std::size_t depth = 0) noexcept;

// Why JsonView::parse() rejects `input` and where it stopped reading, as
// `<reason> at line <line>, column <column>`, such as `expected ':' at line 1,
// column 7`: what follows the colon in that rejection's message. The column
// counts code points. Empty when `input` is one JSON document. Its words are a
// fixed phrase and numbers derived from the input, so a model may see it.
[[nodiscard]] std::string describe_invalid_json(std::string_view input);

// Appends the canonical form of `text` to `output`: object keys in lexical
// order, no insignificant whitespace, and the canonical spelling of every number
// and string escape, as JsonView::to_json() writes it. The text is to stand
// inside `depth` open arrays and objects. `invalid`, with `output` unchanged,
// when the text is not one JSON document, and `too_deep`, with `output`
// unchanged, when it is one whose canonical form, which keeps only the last of
// an object's members of one key, would nest past json_max_depth there.
[[nodiscard]] json_validity append_canonical_json(std::string& output,
                                                  std::string_view text,
                                                  std::size_t depth = 0);

// Appends `text` as a canonical JSON string literal, quotes included, as
// escape_json_string() spells it. False, with `output` unchanged, when `text` is
// not UTF-8, which no JSON string holds.
[[nodiscard]] bool append_utf8_json_string(std::string& output, std::string_view text);

// One value of a parsed document, borrowed: what a JsonView shows, without a
// share in the document's ownership. A JsonView is a std::shared_ptr, so reading
// a document through views costs two atomic operations for every value visited;
// the reflected decoder walks nodes instead, under the view it was given.
//
// A node is valid for as long as any JsonView into its document is. find()
// returns a node with no value for a member that is absent, which converts to
// false; every other function requires a value.
class json_node {
public:
  // The value `view` shows; a default-constructed view shows a null.
  explicit json_node(const JsonView& view) noexcept;

  [[nodiscard]] explicit operator bool() const noexcept { return value_ != nullptr; }

  // As JsonView's accessors of the same names.
  [[nodiscard]] JsonKind kind() const noexcept;
  [[nodiscard]] std::optional<bool> boolean() const noexcept;
  [[nodiscard]] std::optional<std::int64_t> signed_integer() const noexcept;
  [[nodiscard]] std::optional<std::uint64_t> unsigned_integer() const noexcept;
  [[nodiscard]] std::optional<double> number() const noexcept;
  [[nodiscard]] std::optional<std::string_view> string() const noexcept;
  [[nodiscard]] std::size_t size() const noexcept;
  [[nodiscard]] json_node find(std::string_view name) const noexcept;
  [[nodiscard]] Json to_json() const;

  // An array's element. Requires an array and an index below its size().
  [[nodiscard]] json_node at(std::size_t index) const noexcept;

  // An object's key, in lexical order. Requires an object and an index below
  // its size().
  [[nodiscard]] std::string_view key_at(std::size_t index) const noexcept;

private:
  explicit json_node(const void* const value) noexcept : value_{value} {}

  // The document's node, type-erased as JsonView's is.
  const void* value_{};
};

// format_json_number(), appended to `output` in place: the shortest round-trip
// digits, as std::to_chars writes them, but laid out positionally for decimal
// exponents from -4 to 15 and otherwise as `1E-7`, where std::to_chars chooses by
// length and writes `1e-07`. A non-finite value, which JSON cannot spell, is
// written as null.
void append_canonical_json_number(std::string& output, double value);

} // namespace scrydantic::detail
