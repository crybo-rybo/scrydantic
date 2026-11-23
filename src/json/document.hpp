#pragma once

#include <cstddef>
#include <cstdint>
#include <expected>
#include <new>
#include <optional>
#include <scrydantic/detail/json_text.hpp>
#include <scrydantic/json.hpp>
#include <span>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>

// Scrydantic's own strict JSON layer: an immutable document, a single-pass
// parser, an allocation-free validator that accepts exactly what the parser
// accepts, and the canonical writer.
//
// What the parser accepts is RFC 8259 JSON text holding exactly one value, with
// these decisions where the RFC leaves room:
//
// - Nesting: at most max_depth arrays and objects open at once, counting an
//   empty one. Deeper input is rejected, which bounds the recursion of the
//   parser and of everything that walks a document.
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
inline constexpr std::size_t max_depth = max_json_depth;

class Value;
struct Member;

// The elements of an array, in the arena of the document that holds it.
using Array = std::span<const Value>;
// The members of an object, there too: sorted by key in lexical byte order,
// keys unique.
using Object = std::span<const Member>;

// One immutable JSON value, sixteen bytes: a scalar, or the place and size of a
// string's text or a container's contents in the arena of the Document it was
// parsed into. Copying a value copies those sixteen bytes and nothing they
// point at, so a value and its copies are valid for as long as that Document.
// Only the parser builds one with content; a default-constructed value is null.
// An object's members are sorted, so lookup is a binary search and ordered
// iteration is the canonical order.
class Value {
public:
  constexpr Value() noexcept = default;

  [[nodiscard]] JsonKind kind() const noexcept;

  [[nodiscard]] bool is_null() const noexcept;

  [[nodiscard]] std::optional<bool> boolean() const noexcept;

  // Only for kind() == JsonKind::signed_integer.
  [[nodiscard]] std::optional<std::int64_t> signed_integer() const noexcept;

  // Only for kind() == JsonKind::unsigned_integer.
  [[nodiscard]] std::optional<std::uint64_t> unsigned_integer() const noexcept;

  // Any numeric kind, as a double; an integer converts to the nearest double.
  [[nodiscard]] std::optional<double> number() const noexcept;

  // Borrowed from the document.
  [[nodiscard]] std::optional<std::string_view> string() const noexcept;

  // An array's elements, borrowed from the document; none for any other kind.
  [[nodiscard]] Array elements() const noexcept;

  // An object's members, borrowed from the document and in key order; none for
  // any other kind.
  [[nodiscard]] Object members() const noexcept;

  // The element count of an array or member count of an object; zero otherwise.
  [[nodiscard]] std::size_t size() const noexcept;

  // An object member's value, found by binary search; null when this is not an
  // object or has no such key.
  [[nodiscard]] const Value* find(std::string_view key) const noexcept;

private:
  friend class Builder;

  // What a kind holds: the scalar itself, or where a string's bytes, an
  // array's elements, or an object's members start.
  union Payload {
    std::uint64_t unsigned_integer;
    std::int64_t signed_integer;
    double number;
    bool boolean;
    const char* text;
    const Value* elements;
    const Member* members;
  };

  // The kind takes the low byte of meta_, and a string's length or a
  // container's count the rest, which is room for any that fits in memory.
  static constexpr unsigned int kind_bits = 8;

  constexpr Value(const JsonKind kind, const std::size_t size,
                  const Payload payload) noexcept
      : payload_{payload}, meta_{(static_cast<std::uint64_t>(size) << kind_bits) |
                                 std::to_underlying(kind)} {}

  // The length or count that meta_ holds.
  [[nodiscard]] constexpr std::size_t extent() const noexcept {
    return static_cast<std::size_t>(meta_ >> kind_bits);
  }

  Payload payload_{};
  // Zero is a null: JsonKind::null is the enumeration's zero.
  std::uint64_t meta_{};
};

static_assert(sizeof(Value) == 16);
static_assert(std::is_trivially_copyable_v<Value>);

struct Member {
  std::string_view key{};
  Value value{};
};

// The memory that a document's values point into: the text of its strings and
// the contents of its containers. It hands out pieces of chunks that it never
// moves and frees only when it is destroyed, so a pointer into it stays valid
// when the arena itself moves. Each chunk is twice the size of the one before,
// up to max_chunk, so a document takes a handful of allocations whatever it
// holds, where a tree of strings and vectors takes one or more for each of
// them.
class Arena {
public:
  // `first_chunk` is the size in bytes of the first chunk, which is allocated
  // when something first asks for memory.
  explicit Arena(const std::size_t first_chunk) noexcept : next_chunk_{first_chunk} {}

  Arena(Arena&& other) noexcept
      : chunks_{std::exchange(other.chunks_, nullptr)},
        base_{std::exchange(other.base_, nullptr)},
        used_{std::exchange(other.used_, 0)},
        capacity_{std::exchange(other.capacity_, 0)}, next_chunk_{other.next_chunk_} {}

  Arena& operator=(Arena&& other) noexcept {
    if (this != &other) {
      release();
      chunks_ = std::exchange(other.chunks_, nullptr);
      base_ = std::exchange(other.base_, nullptr);
      used_ = std::exchange(other.used_, 0);
      capacity_ = std::exchange(other.capacity_, 0);
      next_chunk_ = other.next_chunk_;
    }
    return *this;
  }

  Arena(const Arena&) = delete;
  Arena& operator=(const Arena&) = delete;

  ~Arena() { release(); }

  // Uninitialized storage for `count` objects of T, which the arena never
  // destroys: T is one of the trivially destructible types a document is made
  // of.
  template <class T> [[nodiscard]] T* allocate(const std::size_t count) {
    static_assert(std::is_trivially_destructible_v<T>);
    static_assert(alignof(T) <= chunk_alignment);
    return static_cast<T*>(take(count * sizeof(T), std::align_val_t{alignof(T)}));
  }

private:
  // The largest chunk the doubling reaches; a single request for more gets a
  // chunk of its own size.
  static constexpr std::size_t max_chunk = std::size_t{1} << 20U;
  // What a chunk's bytes are aligned for.
  static constexpr std::size_t chunk_alignment = alignof(std::max_align_t);

  // The header of a chunk, whose bytes follow it.
  struct alignas(chunk_alignment) Chunk {
    Chunk* next{};
  };

  [[nodiscard]] void* take(std::size_t bytes, std::align_val_t alignment);
  void add_chunk(std::size_t bytes);
  void release() noexcept;

  // Every chunk, the newest first.
  Chunk* chunks_{};
  // The newest chunk's bytes, how many of them are taken, and how many there
  // are.
  char* base_{};
  std::size_t used_{};
  std::size_t capacity_{};
  std::size_t next_chunk_{};
};

// A parsed document: its root value, which a Document is, and the arena that
// everything under the root lives in. Moving a document moves neither the
// values nor the text, so pointers and views into it stay valid; they end with
// the document that was moved to.
class Document : public Value {
public:
  Document(const Value root, Arena arena) noexcept
      : Value{root}, arena_{std::move(arena)} {}

private:
  Arena arena_;
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
[[nodiscard]] std::expected<Document, Rejection> parse(std::string_view text);

// True exactly when parse(text) would succeed, without allocating.
[[nodiscard]] bool validate(std::string_view text) noexcept;

// What parse(text) would reject, without allocating: the same rejection, or
// empty when it would succeed. `depth` is how many arrays and objects are open
// around the text where it is to stand; they count toward max_depth, so with a
// depth above zero the text may be found too deep where parse(text) accepts it.
[[nodiscard]] std::optional<Rejection> diagnose(std::string_view text,
                                                std::size_t depth = 0) noexcept;

using Validity = json_validity;

// What diagnose(text, depth) decides, and when it rejects the text, whether
// reading stopped at a container nested deeper than max_depth.
[[nodiscard]] Validity check(std::string_view text, std::size_t depth = 0) noexcept;

// How deep `value` nests arrays and objects, counting an empty one: zero for a
// scalar, and at most max_depth.
[[nodiscard]] std::size_t nesting(const Value& value) noexcept;

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

// append_quoted() for text that may not be UTF-8: false, with `output`
// unchanged, when it is not, by the rules the parser holds a string to.
[[nodiscard]] bool append_quoted_utf8(std::string& output, std::string_view text);

} // namespace scrydantic::detail::json
