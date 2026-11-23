#include "json/core.hpp" // IWYU pragma: keep

#include "json/document.hpp"

#include "json/number.hpp"

#include <algorithm>
#include <array>
#include <charconv>
#include <compare>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <expected>
#include <limits>
#include <memory>
#include <new>
#include <optional>
#include <scrydantic/detail/json_text.hpp>
#include <scrydantic/json.hpp>
#include <span>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

namespace scrydantic::detail::json {

// The parser's only way to give a Value content.
class Builder {
public:
  [[nodiscard]] static Value make(std::nullptr_t) noexcept { return {}; }

  [[nodiscard]] static Value make(const bool data) noexcept {
    return {JsonKind::boolean, 0, {.boolean = data}};
  }

  [[nodiscard]] static Value make(const std::int64_t data) noexcept {
    return {JsonKind::signed_integer, 0, {.signed_integer = data}};
  }

  [[nodiscard]] static Value make(const std::uint64_t data) noexcept {
    return {JsonKind::unsigned_integer, 0, {.unsigned_integer = data}};
  }

  [[nodiscard]] static Value make(const double data) noexcept {
    return {JsonKind::number, 0, {.number = data}};
  }

  [[nodiscard]] static Value make(const std::string_view data) noexcept {
    Value::Payload payload{};
    payload.text = data.data();
    return {JsonKind::string, data.size(), payload};
  }

  [[nodiscard]] static Value make(const Array data) noexcept {
    return {JsonKind::array, data.size(), {.elements = data.data()}};
  }

  [[nodiscard]] static Value make(const Object data) noexcept {
    return {JsonKind::object, data.size(), {.members = data.data()}};
  }
};

void* Arena::take(const std::size_t bytes, const std::align_val_t alignment) {
  // A chunk's bytes start aligned for anything, so an offset into them that is
  // a multiple of an alignment is aligned for it.
  const auto multiple = static_cast<std::size_t>(alignment);
  std::size_t start = (used_ + multiple - 1U) & ~(multiple - 1U);
  if (start > capacity_ || bytes > capacity_ - start) {
    add_chunk(bytes);
    start = 0;
  }
  used_ = start + bytes;
  return base_ + start;
}

void Arena::add_chunk(const std::size_t bytes) {
  const std::size_t size = std::max(next_chunk_, bytes);
  next_chunk_ = std::min(size * 2U, max_chunk);
  void* const memory = ::operator new(sizeof(Chunk) + size);
  chunks_ = ::new (memory) Chunk{.next = chunks_};
  base_ = static_cast<char*>(memory) + sizeof(Chunk);
  used_ = 0;
  capacity_ = size;
}

void Arena::release() noexcept {
  while (chunks_ != nullptr) {
    Chunk* const next = chunks_->next;
    ::operator delete(chunks_);
    chunks_ = next;
  }
}

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

// The first chunk of the arena of a document parsed from `size` bytes of text.
// A document's values and text take from about once to three times the size of
// its text, so most documents fit in the first chunk and the one after it.
[[nodiscard]] std::size_t first_chunk_for(const std::size_t size) noexcept {
  constexpr std::size_t smallest = 256;
  constexpr std::size_t largest = std::size_t{1} << 16U;
  return std::clamp(size, smallest, largest);
}

// A copy of [first, last) in `arena`.
[[nodiscard]] std::string_view stored_text(Arena& arena, const char* const first,
                                           const char* const last) {
  const auto size = static_cast<std::size_t>(last - first);
  if (size == 0) {
    // Not a null view: an empty string is a string.
    return std::string_view{""};
  }
  auto* const stored = arena.allocate<char>(size);
  std::memcpy(stored, first, size);
  return {stored, size};
}

// A copy of `values` in `arena`: the contents of a finished container.
template <class T>
[[nodiscard]] std::span<const T> stored_contents(Arena& arena,
                                                 const std::span<const T> values) {
  if (values.empty()) {
    return {};
  }
  auto* const stored = arena.allocate<T>(values.size());
  std::uninitialized_copy_n(values.data(), values.size(), stored);
  return {stored, values.size()};
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

// Whether `members` are in key order with no key twice, as an object's are.
[[nodiscard]] bool in_key_order(const std::span<const Member> members) noexcept {
  return std::ranges::adjacent_find(members,
                                    [](const Member& left, const Member& right) {
                                      return !(left.key < right.key);
                                    }) == members.end();
}

// Stores in `arena` the object of `members`, which are in document order:
// sorted by key, keeping the last of each run of equal keys. Members already in
// order are copied as they are. Otherwise their positions are sorted, in
// `order`, rather than the members themselves, and equal keys by position, so
// the last of a run is the last occurrence.
[[nodiscard]] Object stored_object(Arena& arena, const std::span<const Member> members,
                                   std::vector<std::size_t>& order) {
  if (in_key_order(members)) {
    return stored_contents(arena, members);
  }
  order.resize(members.size());
  for (std::size_t position = 0; position < order.size(); ++position) {
    order[position] = position;
  }
  std::ranges::sort(order, [members](const std::size_t left, const std::size_t right) {
    const auto compared = members[left].key <=> members[right].key;
    return std::is_eq(compared) ? left < right : std::is_lt(compared);
  });
  // Room for every member; the few that an equal key replaces go unused.
  auto* const stored = arena.allocate<Member>(members.size());
  std::size_t kept = 0;
  for (std::size_t index = 0; index < order.size(); ++index) {
    const Member& member = members[order[index]];
    const bool replaced =
        index + 1 < order.size() && members[order[index + 1]].key == member.key;
    if (!replaced) {
      std::construct_at(stored + kept, member);
      ++kept;
    }
  }
  return {stored, kept};
}

// What the parser keeps while it reads, and the validator has no use for.
struct Workspace {
  explicit Workspace(Arena& document_arena) noexcept : arena{&document_arena} {}

  // Where finished strings and containers go.
  Arena* arena;
  // The elements and the members of every container still open, each
  // container's above its parent's, so that a finished container is stored in
  // one piece of exactly its size.
  std::vector<Value> values{};
  std::vector<Member> members{};
  // stored_object()'s positions.
  std::vector<std::size_t> order{};
  // The text of a string that has an escape, while it is read.
  std::string text{};
};

// The capacity a workspace's stack starts with: enough for most documents, so
// that a small one, such as a tool's arguments, grows it once and not at each
// power of two on the way.
constexpr std::size_t initial_stack = 16;

template <class T> void push(std::vector<T>& stack, const T& value) {
  if (stack.capacity() == 0) {
    stack.reserve(initial_stack);
  }
  stack.push_back(value);
}

// One recursive-descent reader for both the parser and the validator, so the
// two cannot disagree about the grammar, nor about where and why they reject a
// text. Build selects whether it produces a Value; without it the reader touches
// no heap memory. Every failure goes through fail(), which records the
// Rejection, so the success path does no work for it.
template <bool Build> class Reader {
public:
  using Out = std::conditional_t<Build, Value, Discard>;
  using Text = std::conditional_t<Build, std::string_view, Discard>;
  using Work = std::conditional_t<Build, Workspace, Discard>;

  // The validator. `depth` arrays and objects are already open around the text.
  explicit Reader(const std::string_view text, const std::size_t depth = 0) noexcept
    requires(!Build)
      : begin_(text.data()), cursor_(text.data()), end_(text.data() + text.size()),
        depth_(depth) {}

  // The parser, whose values point into `arena`.
  Reader(const std::string_view text, Arena& arena) noexcept
    requires Build
      : begin_(text.data()), cursor_(text.data()), end_(text.data() + text.size()),
        work_(arena) {}

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

  template <class T> static void emit(Out& out, const T data) {
    if constexpr (Build) {
      out = Builder::make(data);
    }
  }

  // How many elements of the arrays still open the workspace holds, which is
  // where those of an array opened now start; open_members() is the same for
  // objects.
  [[nodiscard]] std::size_t open_values() const noexcept {
    if constexpr (Build) {
      return work_.values.size();
    } else {
      return 0;
    }
  }

  [[nodiscard]] std::size_t open_members() const noexcept {
    if constexpr (Build) {
      return work_.members.size();
    } else {
      return 0;
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
    [[maybe_unused]] const std::size_t first = open_values();
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
        push(work_.values, element);
      }
    }
    --depth_;
    if constexpr (Build) {
      const std::span<const Value> elements{work_.values};
      emit(out, stored_contents(*work_.arena, elements.subspan(first)));
      work_.values.resize(first);
    }
    return true;
  }

  [[nodiscard]] bool member() {
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
      push(work_.members, Member{.key = key, .value = element});
    }
    return true;
  }

  [[nodiscard]] bool object(Out& out) {
    if (!enter()) {
      return false;
    }
    [[maybe_unused]] const std::size_t first = open_members();
    skip_whitespace();
    bool closed = next_is('}');
    if (closed) {
      ++cursor_;
    }
    while (!closed) {
      if (!member() || !separator('}', closed)) {
        return false;
      }
    }
    --depth_;
    if constexpr (Build) {
      const std::span<const Member> members{work_.members};
      emit(out, stored_object(*work_.arena, members.subspan(first), work_.order));
      work_.members.resize(first);
    }
    return true;
  }

  [[nodiscard]] bool string_value(Out& out) {
    Text text{};
    if (!read_string(text)) {
      return false;
    }
    emit(out, text);
    return true;
  }

  // Reads a string token, the cursor on its opening quote. A string with no
  // escape, as most are, is copied from the input to the arena in one piece.
  // One with an escape is first put together in the workspace, the runs of
  // bytes between its escapes copied whole.
  [[nodiscard]] bool read_string(Text& text) {
    const char* const quote = cursor_++;
    // The bytes since the opening quote or the last escape.
    const char* run = cursor_;
    bool escaped = false;
    while (cursor_ != end_) {
      if (remaining() >= word_size && word_is_plain(load_word(cursor_))) {
        cursor_ += word_size;
        continue;
      }
      const auto byte = static_cast<unsigned char>(*cursor_);
      if (byte == '"') {
        finish_text(text, run, escaped);
        ++cursor_;
        return true;
      }
      if (byte == '\\') {
        keep_run(run, escaped);
        if (!escape()) {
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

  // Before an escape is read: adds the run that it ends to the text in the
  // workspace, which a string's first escape starts over.
  void keep_run(const char* const run, bool& escaped) {
    if constexpr (Build) {
      if (!escaped) {
        work_.text.clear();
        escaped = true;
      }
      work_.text.append(run, cursor_);
    } else {
      static_cast<void>(run);
      static_cast<void>(escaped);
    }
  }

  // At a string's closing quote: stores its text in the arena. That is its
  // last run alone when it had no escape, and otherwise what the workspace
  // holds of it and then that run.
  void finish_text(Text& text, const char* const run, const bool escaped) {
    if constexpr (Build) {
      if (!escaped) {
        text = stored_text(*work_.arena, run, cursor_);
        return;
      }
      work_.text.append(run, cursor_);
      text = stored_text(*work_.arena, work_.text.data(),
                         work_.text.data() + work_.text.size());
    } else {
      static_cast<void>(text);
      static_cast<void>(run);
      static_cast<void>(escaped);
    }
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
  [[nodiscard]] bool escape() {
    const char* const backslash = cursor_++;
    if (at_end()) {
      return fail(Fault::end_of_input, cursor_);
    }
    const char escaped = *cursor_++;
    if (escaped == 'u') {
      return unicode_escape(backslash);
    }
    const char decoded = simple_escape(escaped);
    if (decoded == '\0') {
      return fail(Fault::invalid_escape, backslash);
    }
    if constexpr (Build) {
      work_.text.push_back(decoded);
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

  [[nodiscard]] bool unicode_escape(const char* const backslash) {
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
      append_utf8(work_.text, code_point);
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
  [[no_unique_address]] Work work_{};
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

void write_array(const Array elements, std::string& output) {
  output.push_back('[');
  for (std::size_t index = 0; index < elements.size(); ++index) {
    if (index > 0) {
      output.push_back(',');
    }
    write_value(elements[index], output);
  }
  output.push_back(']');
}

void write_object(const Object members, std::string& output) {
  output.push_back('{');
  for (std::size_t index = 0; index < members.size(); ++index) {
    if (index > 0) {
      output.push_back(',');
    }
    append_quoted(output, members[index].key);
    output.push_back(':');
    write_value(members[index].value, output);
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
  switch (value.kind()) {
  case JsonKind::array:
    write_array(value.elements(), output);
    break;
  case JsonKind::object:
    write_object(value.members(), output);
    break;
  default:
    write_scalar(value, output);
    break;
  }
}

} // namespace

JsonKind Value::kind() const noexcept {
  constexpr std::uint64_t kind_mask = (std::uint64_t{1} << kind_bits) - 1U;
  return static_cast<JsonKind>(meta_ & kind_mask);
}

bool Value::is_null() const noexcept { return kind() == JsonKind::null; }

std::optional<bool> Value::boolean() const noexcept {
  if (kind() != JsonKind::boolean) {
    return std::nullopt;
  }
  return payload_.boolean;
}

std::optional<std::int64_t> Value::signed_integer() const noexcept {
  if (kind() != JsonKind::signed_integer) {
    return std::nullopt;
  }
  return payload_.signed_integer;
}

std::optional<std::uint64_t> Value::unsigned_integer() const noexcept {
  if (kind() != JsonKind::unsigned_integer) {
    return std::nullopt;
  }
  return payload_.unsigned_integer;
}

std::optional<double> Value::number() const noexcept {
  switch (kind()) {
  case JsonKind::number:
    return payload_.number;
  case JsonKind::unsigned_integer:
    return static_cast<double>(payload_.unsigned_integer);
  case JsonKind::signed_integer:
    return static_cast<double>(payload_.signed_integer);
  default:
    return std::nullopt;
  }
}

std::optional<std::string_view> Value::string() const noexcept {
  if (kind() != JsonKind::string) {
    return std::nullopt;
  }
  return std::string_view{payload_.text, extent()};
}

Array Value::elements() const noexcept {
  if (kind() != JsonKind::array) {
    return {};
  }
  return {payload_.elements, extent()};
}

Object Value::members() const noexcept {
  if (kind() != JsonKind::object) {
    return {};
  }
  return {payload_.members, extent()};
}

std::size_t Value::size() const noexcept {
  const JsonKind held = kind();
  return held == JsonKind::array || held == JsonKind::object ? extent() : 0;
}

const Value* Value::find(const std::string_view key) const noexcept {
  const Object all = members();
  const auto found = std::ranges::lower_bound(all, key, {}, &Member::key);
  if (found == all.end() || found->key != key) {
    return nullptr;
  }
  return &found->value;
}

std::expected<Document, Rejection> parse(const std::string_view text) {
  Arena arena{first_chunk_for(text.size())};
  Value root{};
  Reader<true> reader{text, arena};
  if (!reader.document(root)) {
    return std::unexpected(reader.rejection());
  }
  return Document{root, std::move(arena)};
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
  switch (value.kind()) {
  case JsonKind::array:
    for (const auto& element : value.elements()) {
      deepest = std::max(deepest, nesting(element));
    }
    return deepest + 1;
  case JsonKind::object:
    for (const auto& member : value.members()) {
      deepest = std::max(deepest, nesting(member.value));
    }
    return deepest + 1;
  default:
    return 0;
  }
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
