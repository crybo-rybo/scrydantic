#include "json/core.hpp" // IWYU pragma: keep

#include "json/document.hpp"

#include "json/number.hpp"

#include <algorithm>
#include <array>
#include <charconv>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <expected>
#include <iterator>
#include <limits>
#include <optional>
#include <scrydantic/detail/json_text.hpp>
#include <scrydantic/json.hpp>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <variant>

namespace scrydantic::detail::json {

// The parser's only way to give a Value content.
class Builder {
public:
  template <class T> [[nodiscard]] static Value make(T data) {
    return Value{Value::Data{std::in_place_type<T>, std::move(data)}};
  }
};

namespace {

// Stands in for the value under construction when the reader only validates.
struct Discard {};

[[nodiscard]] bool is_whitespace(const char character) noexcept {
  return character == ' ' || character == '\t' || character == '\n' ||
         character == '\r';
}

[[nodiscard]] bool is_digit(const char character) noexcept {
  return character >= '0' && character <= '9';
}

// The value of one hexadecimal digit, or -1.
[[nodiscard]] int hex_value(const char character) noexcept {
  if (is_digit(character)) {
    return character - '0';
  }
  if (character >= 'a' && character <= 'f') {
    return character - 'a' + 10;
  }
  if (character >= 'A' && character <= 'F') {
    return character - 'A' + 10;
  }
  return -1;
}

// String bodies are scanned eight bytes at a time until a word holds a byte
// that needs a closer look. The byte-wise tests are the exact "has less than"
// and "has zero" word tricks, valid for bounds up to 0x80.
constexpr std::size_t word_size = sizeof(std::uint64_t);
constexpr std::uint64_t byte_ones = 0x0101010101010101U;
constexpr std::uint64_t byte_highs = 0x8080808080808080U;

[[nodiscard]] std::uint64_t load_word(const char* bytes) noexcept {
  std::uint64_t word = 0;
  std::memcpy(&word, bytes, word_size);
  return word;
}

[[nodiscard]] constexpr std::uint64_t bytes_below(const std::uint64_t word,
                                                  const std::uint64_t bound) noexcept {
  return (word - (byte_ones * bound)) & ~word & byte_highs;
}

[[nodiscard]] constexpr std::uint64_t bytes_equal(const std::uint64_t word,
                                                  const unsigned char byte) noexcept {
  return bytes_below(word ^ (byte_ones * byte), 1U);
}

// A control character, a quote, or a backslash: what quoting must escape.
[[nodiscard]] constexpr bool word_needs_escape(const std::uint64_t word) noexcept {
  return (bytes_below(word, 0x20U) | bytes_equal(word, '"') |
          bytes_equal(word, '\\')) != 0;
}

// Eight bytes a string body may hold as they are: ASCII needing no escape.
[[nodiscard]] constexpr bool word_is_plain(const std::uint64_t word) noexcept {
  return (word & byte_highs) == 0 && !word_needs_escape(word);
}

void append_utf8(std::string& output, const std::uint32_t code_point) {
  const auto byte = [](const std::uint32_t value) { return static_cast<char>(value); };
  if (code_point < 0x80U) {
    output.push_back(byte(code_point));
  } else if (code_point < 0x800U) {
    output.push_back(byte(0xC0U | (code_point >> 6U)));
    output.push_back(byte(0x80U | (code_point & 0x3FU)));
  } else if (code_point < 0x10000U) {
    output.push_back(byte(0xE0U | (code_point >> 12U)));
    output.push_back(byte(0x80U | ((code_point >> 6U) & 0x3FU)));
    output.push_back(byte(0x80U | (code_point & 0x3FU)));
  } else {
    output.push_back(byte(0xF0U | (code_point >> 18U)));
    output.push_back(byte(0x80U | ((code_point >> 12U) & 0x3FU)));
    output.push_back(byte(0x80U | ((code_point >> 6U) & 0x3FU)));
    output.push_back(byte(0x80U | (code_point & 0x3FU)));
  }
}

[[nodiscard]] char simple_escape(const char escape) noexcept {
  switch (escape) {
  case '"':
  case '\\':
  case '/':
    return escape;
  case 'b':
    return '\b';
  case 'f':
    return '\f';
  case 'n':
    return '\n';
  case 'r':
    return '\r';
  case 't':
    return '\t';
  default:
    return '\0';
  }
}

// Sorts an object's members by key and keeps the last of each run of equal
// keys. The stable sort leaves duplicates in document order, so the last of a
// run is the last occurrence.
void canonicalize_members(Object& members) {
  const auto out_of_order = [](const Member& left, const Member& right) {
    return !(left.first < right.first);
  };
  if (std::ranges::adjacent_find(members, out_of_order) == members.end()) {
    return;
  }
  std::ranges::stable_sort(members, [](const Member& left, const Member& right) {
    return left.first < right.first;
  });
  auto kept = members.begin();
  for (auto member = members.begin(); member != members.end(); ++member) {
    const auto next = std::next(member);
    if (next != members.end() && next->first == member->first) {
      continue;
    }
    if (kept != member) {
      *kept = std::move(*member);
    }
    ++kept;
  }
  members.erase(kept, members.end());
}

// One recursive-descent reader for both the parser and the validator, so the
// two cannot disagree about the grammar, nor about where and why they reject a
// text. Build selects whether it produces a Value; without it the reader touches
// no heap memory. Every failure goes through fail(), which records the
// Rejection, so the success path does no work for it.
template <bool Build> class Reader {
public:
  using Out = std::conditional_t<Build, Value, Discard>;
  using Text = std::conditional_t<Build, std::string, Discard>;
  using Elements = std::conditional_t<Build, Array, Discard>;
  using Members = std::conditional_t<Build, Object, Discard>;

  // `depth` arrays and objects are already open around the text.
  explicit Reader(const std::string_view text, const std::size_t depth = 0) noexcept
      : begin_(text.data()), cursor_(text.data()), end_(text.data() + text.size()),
        depth_(depth) {}

  [[nodiscard]] bool document(Out& out) {
    skip_whitespace();
    if (at_end()) {
      return fail(Fault::empty_input, cursor_);
    }
    if (!value(out)) {
      return false;
    }
    skip_whitespace();
    return at_end() || fail(Fault::text_after_value, cursor_);
  }

  // After document() fails: where and why it stopped. Reading stops at the
  // first problem, so the text may also break another rule further on.
  [[nodiscard]] Rejection rejection() const noexcept {
    return {.fault = fault_, .offset = static_cast<std::size_t>(stop_ - begin_)};
  }

private:
  // Records why reading stops and at which byte, and fails the read. Cold, so
  // that GCC lays out every path into it as the unlikely one.
  [[gnu::cold]] [[nodiscard]] bool fail(const Fault fault,
                                        const char* const at) noexcept {
    fault_ = fault;
    stop_ = at;
    return false;
  }

  // Fails at the cursor with `fault`, or with end_of_input when the text ends
  // there instead.
  [[nodiscard]] bool fail_here(const Fault fault) noexcept {
    return fail(at_end() ? Fault::end_of_input : fault, cursor_);
  }

  [[nodiscard]] bool at_end() const noexcept { return cursor_ == end_; }

  // The bytes left between the cursor and the end of the text.
  [[nodiscard]] std::size_t remaining() const noexcept {
    return static_cast<std::size_t>(end_ - cursor_);
  }

  [[nodiscard]] bool next_is(const char character) const noexcept {
    return cursor_ != end_ && *cursor_ == character;
  }

  void skip_whitespace() noexcept {
    while (cursor_ != end_ && is_whitespace(*cursor_)) {
      ++cursor_;
    }
  }

  template <class T> static void emit(Out& out, T&& data) {
    if constexpr (Build) {
      out = Builder::make<std::remove_cvref_t<T>>(std::forward<T>(data));
    }
  }

  [[nodiscard]] bool value(Out& out) {
    skip_whitespace();
    if (at_end()) {
      return fail(Fault::end_of_input, cursor_);
    }
    switch (*cursor_) {
    case '{':
      return object(out);
    case '[':
      return array(out);
    case '"':
      return string_value(out);
    case 't':
      return literal("true", out, true);
    case 'f':
      return literal("false", out, false);
    case 'n':
      return literal("null", out, nullptr);
    default:
      return number(out);
    }
  }

  template <class T>
  [[nodiscard]] bool literal(const std::string_view word, Out& out, T data) {
    if (remaining() < word.size() ||
        std::memcmp(cursor_, word.data(), word.size()) != 0) {
      return wrong_literal(word);
    }
    cursor_ += word.size();
    emit(out, data);
    return true;
  }

  // Fails a literal whose bytes are not `word`: the text ends too early when
  // all that is left of it begins the word, and otherwise there is no value at
  // the literal's first byte.
  [[nodiscard]] bool wrong_literal(const std::string_view word) noexcept {
    const std::string_view rest{cursor_, remaining()};
    if (rest.size() < word.size() && word.starts_with(rest)) {
      return fail(Fault::end_of_input, end_);
    }
    return fail(Fault::expected_value, cursor_);
  }

  // Opens an array or object, the cursor on its bracket.
  [[nodiscard]] bool enter() noexcept {
    if (++depth_ > max_depth) {
      return fail(Fault::too_deep, cursor_);
    }
    ++cursor_;
    return true;
  }

  // After an element or member: a comma continues the container and the
  // closing character ends it.
  [[nodiscard]] bool separator(const char close, bool& closed) noexcept {
    skip_whitespace();
    if (at_end()) {
      return fail(Fault::end_of_input, cursor_);
    }
    const char character = *cursor_;
    closed = character == close;
    if (!closed && character != ',') {
      return fail(close == ']' ? Fault::expected_comma_or_bracket
                               : Fault::expected_comma_or_brace,
                  cursor_);
    }
    ++cursor_;
    return true;
  }

  [[nodiscard]] bool array(Out& out) {
    if (!enter()) {
      return false;
    }
    Elements elements{};
    skip_whitespace();
    bool closed = next_is(']');
    if (closed) {
      ++cursor_;
    }
    while (!closed) {
      Out element{};
      if (!value(element) || !separator(']', closed)) {
        return false;
      }
      if constexpr (Build) {
        elements.push_back(std::move(element));
      }
    }
    --depth_;
    emit(out, std::move(elements));
    return true;
  }

  [[nodiscard]] bool member(Members& members) {
    skip_whitespace();
    Text key{};
    if (!next_is('"')) {
      return fail_here(Fault::expected_key);
    }
    if (!read_string(key)) {
      return false;
    }
    skip_whitespace();
    if (!next_is(':')) {
      return fail_here(Fault::expected_colon);
    }
    ++cursor_;
    Out element{};
    if (!value(element)) {
      return false;
    }
    if constexpr (Build) {
      members.emplace_back(std::move(key), std::move(element));
    }
    return true;
  }

  [[nodiscard]] bool object(Out& out) {
    if (!enter()) {
      return false;
    }
    Members members{};
    skip_whitespace();
    bool closed = next_is('}');
    if (closed) {
      ++cursor_;
    }
    while (!closed) {
      if (!member(members) || !separator('}', closed)) {
        return false;
      }
    }
    --depth_;
    if constexpr (Build) {
      canonicalize_members(members);
    }
    emit(out, std::move(members));
    return true;
  }

  [[nodiscard]] bool string_value(Out& out) {
    Text text{};
    if (!read_string(text)) {
      return false;
    }
    emit(out, std::move(text));
    return true;
  }

  // Reads a string token, the cursor on its opening quote. Runs of bytes that
  // need no decoding are copied whole.
  [[nodiscard]] bool read_string(Text& text) {
    const char* const quote = cursor_++;
    const char* run = cursor_;
    const auto flush = [&] {
      if constexpr (Build) {
        text.append(run, cursor_);
      }
    };
    while (cursor_ != end_) {
      if (remaining() >= word_size && word_is_plain(load_word(cursor_))) {
        cursor_ += word_size;
        continue;
      }
      const auto byte = static_cast<unsigned char>(*cursor_);
      if (byte == '"') {
        flush();
        ++cursor_;
        return true;
      }
      if (byte == '\\') {
        flush();
        if (!escape(text)) {
          return fail_inside(quote);
        }
        run = cursor_;
        continue;
      }
      if (byte < 0x20U) {
        return fail(Fault::control_character, cursor_);
      }
      if (byte < 0x80U) {
        ++cursor_;
      } else if (!utf8_sequence(byte)) {
        return fail_inside(quote);
      }
    }
    return fail(Fault::unterminated_string, quote);
  }

  // Fails the string that opens at `quote` after an escape or a UTF-8 sequence
  // in it failed. One that failed because the text ended leaves the string
  // unterminated, which is what a model cut off mid-string needs to hear.
  [[nodiscard]] bool fail_inside(const char* const quote) noexcept {
    return fault_ == Fault::end_of_input && fail(Fault::unterminated_string, quote);
  }

  // The UTF-8 sequence that `lead`, at the cursor, starts.
  [[nodiscard]] bool utf8_sequence(const unsigned char lead) noexcept {
    const auto expected = utf8_lead_of(lead);
    if (expected.length == 0) {
      return fail(Fault::invalid_utf8, cursor_);
    }
    if (remaining() < expected.length) {
      return cut_short(expected);
    }
    if (!utf8_follows(cursor_, expected, expected.length)) {
      return fail(Fault::invalid_utf8, cursor_);
    }
    cursor_ += expected.length;
    return true;
  }

  // A sequence the text ends inside: the text ends too early only when what
  // there is of the sequence is well formed.
  [[nodiscard]] bool cut_short(const utf8_lead& expected) noexcept {
    return utf8_follows(cursor_, expected, remaining())
               ? fail(Fault::end_of_input, end_)
               : fail(Fault::invalid_utf8, cursor_);
  }

  // The escape whose backslash is at the cursor.
  [[nodiscard]] bool escape(Text& text) {
    const char* const backslash = cursor_++;
    if (at_end()) {
      return fail(Fault::end_of_input, cursor_);
    }
    const char escaped = *cursor_++;
    if (escaped == 'u') {
      return unicode_escape(text, backslash);
    }
    const char decoded = simple_escape(escaped);
    if (decoded == '\0') {
      return fail(Fault::invalid_escape, backslash);
    }
    if constexpr (Build) {
      text.push_back(decoded);
    }
    return true;
  }

  // The four hex digits of the \u escape at `backslash`, the cursor on the
  // first.
  [[nodiscard]] bool hex_quad(const char* const backslash,
                              std::uint32_t& value) noexcept {
    if (remaining() < 4) {
      // The text ends too early, unless a byte before its end is no hex digit.
      const bool digits_so_far = std::ranges::all_of(
          std::string_view{cursor_, remaining()},
          [](const char character) { return hex_value(character) >= 0; });
      return digits_so_far ? fail(Fault::end_of_input, end_)
                           : fail(Fault::invalid_escape, backslash);
    }
    value = 0;
    for (int index = 0; index < 4; ++index) {
      const int digit = hex_value(*cursor_++);
      if (digit < 0) {
        return fail(Fault::invalid_escape, backslash);
      }
      value = (value << 4U) | static_cast<std::uint32_t>(digit);
    }
    return true;
  }

  [[nodiscard]] bool unicode_escape(Text& text, const char* const backslash) {
    std::uint32_t code_point = 0;
    if (!hex_quad(backslash, code_point)) {
      return false;
    }
    if (code_point >= 0xDC00U && code_point <= 0xDFFFU) {
      return fail(Fault::invalid_escape, backslash);
    }
    if (code_point >= 0xD800U && code_point <= 0xDBFFU &&
        !low_surrogate(backslash, code_point)) {
      return false;
    }
    if constexpr (Build) {
      append_utf8(text, code_point);
    }
    return true;
  }

  // The \u escape of the low surrogate that must come straight after the high
  // one, `code_point`, escaped at `backslash`; joins the pair into
  // `code_point`. A broken pair is an invalid escape at the high surrogate.
  [[nodiscard]] bool low_surrogate(const char* const backslash,
                                   std::uint32_t& code_point) noexcept {
    constexpr std::string_view introducer = "\\u";
    const std::string_view next{cursor_, std::min(remaining(), introducer.size())};
    if (next != introducer) {
      return introducer.starts_with(next) ? fail(Fault::end_of_input, end_)
                                          : fail(Fault::invalid_escape, backslash);
    }
    cursor_ += introducer.size();
    std::uint32_t low = 0;
    if (!hex_quad(backslash, low)) {
      return false;
    }
    if (low < 0xDC00U || low > 0xDFFFU) {
      return fail(Fault::invalid_escape, backslash);
    }
    code_point = 0x10000U + ((code_point - 0xD800U) << 10U) + (low - 0xDC00U);
    return true;
  }

  // One or more digits; false when there is none.
  [[nodiscard]] bool digits() noexcept {
    const char* start = cursor_;
    while (cursor_ != end_ && is_digit(*cursor_)) {
      ++cursor_;
    }
    return cursor_ != start;
  }

  // The digits a number requires after a '-', a '.', or an exponent's 'e' and
  // sign.
  [[nodiscard]] bool required_digits() noexcept {
    return digits() || fail_here(Fault::expected_digit);
  }

  [[nodiscard]] bool exponent() noexcept {
    ++cursor_;
    if (next_is('+') || next_is('-')) {
      ++cursor_;
    }
    return required_digits();
  }

  // Scans the RFC 8259 number grammar; true when well formed. `plain` ends false
  // when the number has a fraction or an exponent.
  [[nodiscard]] bool scan_number(bool& plain) noexcept {
    const bool negative = next_is('-');
    if (negative) {
      ++cursor_;
    }
    if (next_is('0')) {
      ++cursor_;
    } else if (!digits()) {
      // Past a '-' a digit is missing. Anywhere else this was never a number:
      // value() sends here every byte that starts no other value.
      return negative ? fail_here(Fault::expected_digit)
                      : fail(Fault::expected_value, cursor_);
    }
    if (next_is('.')) {
      ++cursor_;
      if (!required_digits()) {
        return false;
      }
      plain = false;
    }
    if (next_is('e') || next_is('E')) {
      plain = false;
      return exponent();
    }
    return true;
  }

  [[nodiscard]] bool number(Out& out) {
    const char* start = cursor_;
    bool plain = true;
    if (!scan_number(plain)) {
      return false;
    }
    const std::string_view token{start, cursor_};
    if (plain && integer(token, out)) {
      return true;
    }
    const auto converted = parse_double(start, cursor_);
    if (!converted) {
      return fail(Fault::number_out_of_range, start);
    }
    emit(out, *converted);
    return true;
  }

  // A plain integer token that fits its kind; false sends it to the double path.
  [[nodiscard]] static bool integer(std::string_view token, Out& out) {
    const bool negative = token.front() == '-';
    if (negative) {
      token.remove_prefix(1);
    }
    constexpr auto max = std::numeric_limits<std::uint64_t>::max();
    std::uint64_t magnitude = 0;
    for (const char digit : token) {
      const auto value = static_cast<std::uint64_t>(digit - '0');
      if (magnitude > (max - value) / 10U) {
        return false;
      }
      magnitude = (magnitude * 10U) + value;
    }
    if (!negative) {
      emit(out, magnitude);
      return true;
    }
    constexpr auto min_magnitude = std::uint64_t{1} << 63U;
    if (magnitude == 0 || magnitude > min_magnitude) {
      return false;
    }
    // Two's complement negation; 2^63 becomes the minimum int64.
    emit(out, static_cast<std::int64_t>(~magnitude + 1U));
    return true;
  }

  const char* begin_{};
  const char* cursor_{};
  const char* end_{};
  std::size_t depth_{};
  // What fail() recorded.
  Fault fault_{};
  const char* stop_{};
};

// Each fault's phrase, in Fault order. The phrases are short and fixed, so the
// same text always gives a model the same words to act on.
constexpr std::array<std::string_view, 15> fault_reasons{
    "empty input",         "unexpected end of input", "unexpected text after the value",
    "expected a value",    "expected a string key",   "expected ':'",
    "expected ',' or ']'", "expected ',' or '}'",     "expected a digit",
    "number out of range", "unterminated string",     "unescaped control character",
    "invalid escape",      "invalid UTF-8",           "nesting deeper than 256",
};
static_assert(fault_reasons.size() == std::to_underlying(Fault::too_deep) + 1U);
static_assert(max_depth == 256, "the too_deep phrase spells max_depth");

[[nodiscard]] bool is_continuation(const char byte) noexcept {
  return (static_cast<unsigned char>(byte) & 0xC0U) == 0x80U;
}

// ---------------------------------------------------------------------------
// The canonical writer.
// ---------------------------------------------------------------------------

template <class Integer> void append_integer(std::string& output, const Integer value) {
  std::array<char, 24> buffer{};
  const auto written =
      std::to_chars(buffer.data(), buffer.data() + buffer.size(), value);
  output.append(buffer.data(), written.ptr);
}

[[nodiscard]] const char* short_escape(const unsigned char byte) noexcept {
  switch (byte) {
  case '"':
    return "\\\"";
  case '\\':
    return "\\\\";
  case '\b':
    return "\\b";
  case '\f':
    return "\\f";
  case '\n':
    return "\\n";
  case '\r':
    return "\\r";
  case '\t':
    return "\\t";
  default:
    return nullptr;
  }
}

[[nodiscard]] bool needs_escape(const char character) noexcept {
  const auto byte = static_cast<unsigned char>(character);
  return byte < 0x20U || byte == '"' || byte == '\\';
}

void append_escape(std::string& output, const unsigned char byte) {
  if (const char* escape = short_escape(byte)) {
    output.append(escape);
    return;
  }
  constexpr std::string_view hexadecimal = "0123456789ABCDEF";
  output.append("\\u00");
  output.push_back(hexadecimal[byte >> 4U]);
  output.push_back(hexadecimal[byte & 0x0FU]);
}

void write_value(const Value& value, std::string& output);

void write_array(const Array& elements, std::string& output) {
  output.push_back('[');
  for (std::size_t index = 0; index < elements.size(); ++index) {
    if (index > 0) {
      output.push_back(',');
    }
    write_value(elements[index], output);
  }
  output.push_back(']');
}

void write_object(const Object& members, std::string& output) {
  output.push_back('{');
  for (std::size_t index = 0; index < members.size(); ++index) {
    if (index > 0) {
      output.push_back(',');
    }
    append_quoted(output, members[index].first);
    output.push_back(':');
    write_value(members[index].second, output);
  }
  output.push_back('}');
}

void write_scalar(const Value& value, std::string& output) {
  // Integers before number(), which reports them too.
  if (const auto flag = value.boolean()) {
    output.append(*flag ? "true" : "false");
  } else if (const auto negative = value.signed_integer()) {
    append_integer(output, *negative);
  } else if (const auto natural = value.unsigned_integer()) {
    append_integer(output, *natural);
  } else if (const auto number = value.number()) {
    append_double(output, *number);
  } else if (const auto text = value.string()) {
    append_quoted(output, *text);
  } else {
    output.append("null");
  }
}

void write_value(const Value& value, std::string& output) {
  if (const auto* elements = value.array()) {
    write_array(*elements, output);
  } else if (const auto* members = value.object()) {
    write_object(*members, output);
  } else {
    write_scalar(value, output);
  }
}

} // namespace

JsonKind Value::kind() const noexcept { return static_cast<JsonKind>(data_.index()); }

bool Value::is_null() const noexcept {
  return std::holds_alternative<std::nullptr_t>(data_);
}

std::optional<bool> Value::boolean() const noexcept {
  const auto* value = std::get_if<bool>(&data_);
  return value == nullptr ? std::nullopt : std::optional<bool>{*value};
}

std::optional<std::int64_t> Value::signed_integer() const noexcept {
  const auto* value = std::get_if<std::int64_t>(&data_);
  return value == nullptr ? std::nullopt : std::optional<std::int64_t>{*value};
}

std::optional<std::uint64_t> Value::unsigned_integer() const noexcept {
  const auto* value = std::get_if<std::uint64_t>(&data_);
  return value == nullptr ? std::nullopt : std::optional<std::uint64_t>{*value};
}

std::optional<double> Value::number() const noexcept {
  if (const auto* value = std::get_if<double>(&data_)) {
    return *value;
  }
  if (const auto* value = std::get_if<std::uint64_t>(&data_)) {
    return static_cast<double>(*value);
  }
  if (const auto* value = std::get_if<std::int64_t>(&data_)) {
    return static_cast<double>(*value);
  }
  return std::nullopt;
}

std::optional<std::string_view> Value::string() const noexcept {
  const auto* value = std::get_if<std::string>(&data_);
  return value == nullptr ? std::nullopt : std::optional<std::string_view>{*value};
}

const Array* Value::array() const noexcept { return std::get_if<Array>(&data_); }

const Object* Value::object() const noexcept { return std::get_if<Object>(&data_); }

std::size_t Value::size() const noexcept {
  if (const auto* elements = array()) {
    return elements->size();
  }
  if (const auto* members = object()) {
    return members->size();
  }
  return 0;
}

const Value* Value::find(const std::string_view key) const noexcept {
  const auto* members = object();
  if (members == nullptr) {
    return nullptr;
  }
  const auto found = std::ranges::lower_bound(*members, key, {}, &Member::first);
  if (found == members->end() || found->first != key) {
    return nullptr;
  }
  return &found->second;
}

std::expected<Value, Rejection> parse(const std::string_view text) {
  Value value{};
  Reader<true> reader{text};
  if (!reader.document(value)) {
    return std::unexpected(reader.rejection());
  }
  return value;
}

bool validate(const std::string_view text) noexcept {
  return !diagnose(text).has_value();
}

std::optional<Rejection> diagnose(const std::string_view text,
                                  const std::size_t depth) noexcept {
  Discard discarded{};
  Reader<false> reader{text, depth};
  if (reader.document(discarded)) {
    return std::nullopt;
  }
  return reader.rejection();
}

Validity check(const std::string_view text, const std::size_t depth) noexcept {
  const auto rejection = diagnose(text, depth);
  if (!rejection) {
    return Validity::valid;
  }
  return rejection->fault == Fault::too_deep ? Validity::too_deep : Validity::invalid;
}

std::size_t nesting(const Value& value) noexcept {
  std::size_t deepest = 0;
  if (const auto* elements = value.array()) {
    for (const auto& element : *elements) {
      deepest = std::max(deepest, nesting(element));
    }
    return deepest + 1;
  }
  if (const auto* members = value.object()) {
    for (const auto& member : *members) {
      deepest = std::max(deepest, nesting(member.second));
    }
    return deepest + 1;
  }
  return 0;
}

std::string_view reason(const Fault fault) noexcept {
  return fault_reasons[std::to_underlying(fault)];
}

Position locate(const std::string_view text, const std::size_t offset) noexcept {
  const auto before = text.substr(0, offset);
  const auto line_feed = before.rfind('\n');
  const auto line =
      line_feed == std::string_view::npos ? before : before.substr(line_feed + 1);
  return {
      .line = 1U + static_cast<std::size_t>(std::ranges::count(before, '\n')),
      .column = 1U + static_cast<std::size_t>(std::ranges::count_if(
                         line, [](const char byte) { return !is_continuation(byte); })),
  };
}

std::string describe(const std::string_view text, const Rejection rejection) {
  const auto position = locate(text, rejection.offset);
  std::string description{reason(rejection.fault)};
  description.append(" at line ");
  description.append(std::to_string(position.line));
  description.append(", column ");
  description.append(std::to_string(position.column));
  return description;
}

void write(const Value& value, std::string& output) { write_value(value, output); }

std::string write(const Value& value) {
  std::string output{};
  write_value(value, output);
  return output;
}

void append_quoted(std::string& output, const std::string_view text) {
  output.push_back('"');
  const auto* run = text.data();
  const auto* const end = text.data() + text.size();
  for (const auto* cursor = run; cursor != end;) {
    if (std::cmp_greater_equal(end - cursor, word_size) &&
        !word_needs_escape(load_word(cursor))) {
      cursor += word_size;
      continue;
    }
    if (needs_escape(*cursor)) {
      output.append(run, cursor);
      append_escape(output, static_cast<unsigned char>(*cursor));
      run = cursor + 1;
    }
    ++cursor;
  }
  output.append(run, end);
  output.push_back('"');
}

bool append_quoted_utf8(std::string& output, const std::string_view text) {
  const auto size = output.size();
  output.push_back('"');
  const auto* run = text.data();
  const auto* const end = text.data() + text.size();
  for (const auto* cursor = run; cursor != end;) {
    if (std::cmp_greater_equal(end - cursor, word_size) &&
        word_is_plain(load_word(cursor))) {
      cursor += word_size;
      continue;
    }
    const auto byte = static_cast<unsigned char>(*cursor);
    if (byte >= 0x80U) {
      const auto length = utf8_sequence_length({cursor, end});
      if (length == 0) {
        output.resize(size);
        return false;
      }
      cursor += length;
      continue;
    }
    if (needs_escape(*cursor)) {
      output.append(run, cursor);
      append_escape(output, byte);
      run = cursor + 1;
    }
    ++cursor;
  }
  output.append(run, end);
  output.push_back('"');
  return true;
}

std::string quote(const std::string_view text) {
  std::string output{};
  output.reserve(text.size() + 2U);
  append_quoted(output, text);
  return output;
}

} // namespace scrydantic::detail::json
