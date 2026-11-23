#pragma once

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <iterator>
#include <meta>
#include <optional>
#include <scrydantic/annotations.hpp>
#include <scrydantic/detail/constraints.hpp>
#include <scrydantic/detail/json_number.hpp>
#include <scrydantic/detail/json_string.hpp>
#include <scrydantic/detail/json_text.hpp>
#include <scrydantic/detail/model.hpp>
#include <scrydantic/json.hpp>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

// The annotations that add a keyword to a schema and do nothing else: neither
// encoding nor decoding reads them. A data member or a class carries them. One
// resolution serves the family checks, which reject what cannot be written, and
// the schema generator, which writes the rest.
namespace scrydantic::detail {

// ---- JSON values -------------------------------------------------------------
//
// The value of a scrydantic::keyword is JSON text that goes into a schema as it
// is, so it is read where it is written, at compile time, and by the rules that
// JsonView::parse() reads JSON by (src/json/document.hpp), whose parser is not
// constexpr. It is read only to be checked and to drop the whitespace between
// its tokens. It is not made canonical: its object keys keep their order, a
// repeated key stays, and a number or a string keeps its spelling. Canonical
// form would take a compile-time copy of the JSON core's document and writer,
// which nothing else here needs.

constexpr bool is_json_digit(const char character) {
  return character >= '0' && character <= '9';
}

// The part of a JSON number before its exponent, as a decimal: its digits
// without leading zeros, with the exponent that its fraction digits give them.
constexpr exact_decimal significand_of(const std::string_view spelling) {
  exact_decimal value{};
  bool in_fraction = false;
  for (const char character : spelling) {
    if (character == '.') {
      in_fraction = true;
    } else if (is_json_digit(character)) {
      if (character != '0' || !value.digits.empty()) {
        value.digits.push_back(character);
      }
      value.exponent -= in_fraction ? 1 : 0;
    }
  }
  return value;
}

// The exponent of a JSON number, from the text after its `e`. It is read no
// further than a limit that the digits of no text are many enough to offset.
constexpr std::int64_t exponent_of(const std::string_view spelling) {
  constexpr std::int64_t limit = 1'000'000'000'000'000;
  std::int64_t exponent = 0;
  for (const char character : spelling) {
    if (is_json_digit(character) && exponent < limit) {
      exponent = (exponent * 10) + (character - '0');
    }
  }
  return spelling.starts_with('-') ? -exponent : exponent;
}

// Whether a number with this JSON spelling rounds to a finite double, and not to
// zero unless it is zero; the parser rejects any other. Its first digit says so
// but next to the largest double and the smallest, where the number is compared
// with the halfway point beyond each, from which a tie rounds to the even
// neighbour: infinity and zero.
constexpr bool rounds_to_finite_double(const std::string_view spelling) {
  const auto split = std::min(spelling.find_first_of("eE"), spelling.size());
  auto value = significand_of(spelling.substr(0, split));
  if (value.digits.empty()) {
    return true;
  }
  const auto exponent =
      split < spelling.size() ? exponent_of(spelling.substr(split + 1)) : 0;
  // The value is at least 10^(top - 1) and below 10^top. The largest double is
  // below 10^309, and the smallest above 10^-324.
  const auto top = std::ssize(value.digits) + value.exponent + exponent;
  if (top > -322 && top <= 308) {
    return true;
  }
  if (top > 309 || top < -323) {
    return false;
  }
  value.exponent += static_cast<int>(exponent);
  constexpr std::uint64_t past_largest = (std::uint64_t{1} << 54U) - 1U;
  return compare(value, dyadic_decimal(past_largest, 970)) < 0 &&
         compare(value, dyadic_decimal(1, -1075)) > 0;
}

// Reads JSON from the front of `text`, copying each token to `output` and none
// of the whitespace between tokens. Each read_ function is false where the text
// is not what it reads, and leaves the reader in no particular state.
struct compact_json_reader {
  std::string_view text{};
  std::size_t position{};
  std::string output{};

  // The next character, or a NUL at the end, which no JSON holds unescaped.
  [[nodiscard]] constexpr char peek() const {
    return position < text.size() ? text[position] : '\0';
  }

  constexpr void take() { output.push_back(text[position++]); }

  constexpr bool take_if(const char character) {
    if (peek() != character) {
      return false;
    }
    take();
    return true;
  }

  constexpr void skip_whitespace() {
    while (peek() == ' ' || peek() == '\t' || peek() == '\n' || peek() == '\r') {
      ++position;
    }
  }

  constexpr bool read_word(const std::string_view word) {
    if (!text.substr(position).starts_with(word)) {
      return false;
    }
    output.append(word);
    position += word.size();
    return true;
  }

  constexpr bool read_digits() {
    if (!is_json_digit(peek())) {
      return false;
    }
    while (is_json_digit(peek())) {
      take();
    }
    return true;
  }

  constexpr bool read_number() {
    const auto begin = position;
    take_if('-');
    // A leading zero stands alone.
    if (!take_if('0') && !read_digits()) {
      return false;
    }
    if (take_if('.') && !read_digits()) {
      return false;
    }
    if (take_if('e') || take_if('E')) {
      if (!take_if('+')) {
        take_if('-');
      }
      if (!read_digits()) {
        return false;
      }
    }
    return rounds_to_finite_double(text.substr(begin, position - begin));
  }

  // The four hexadecimal digits of a \u escape.
  constexpr bool read_code_unit(unsigned& unit) {
    unit = 0;
    for (int digit = 0; digit < 4; ++digit) {
      const char character = peek();
      if (is_json_digit(character)) {
        unit = (unit * 16U) + static_cast<unsigned>(character - '0');
      } else if (character >= 'a' && character <= 'f') {
        unit = (unit * 16U) + static_cast<unsigned>(character - 'a') + 10U;
      } else if (character >= 'A' && character <= 'F') {
        unit = (unit * 16U) + static_cast<unsigned>(character - 'A') + 10U;
      } else {
        return false;
      }
      take();
    }
    return true;
  }

  // What follows a backslash. A \u escape of a surrogate is a high one followed
  // at once by a \u escape of a low one.
  constexpr bool read_escape() {
    if (!take_if('u')) {
      constexpr std::string_view short_escapes = "\"\\/bfnrt";
      return short_escapes.contains(peek()) && take_if(peek());
    }
    unsigned unit{};
    if (!read_code_unit(unit) || (unit >= 0xDC00U && unit <= 0xDFFFU)) {
      return false;
    }
    if (unit < 0xD800U || unit > 0xDBFFU) {
      return true;
    }
    unsigned low{};
    return take_if('\\') && take_if('u') && read_code_unit(low) && low >= 0xDC00U &&
           low <= 0xDFFFU;
  }

  constexpr bool read_string() {
    if (!take_if('"')) {
      return false;
    }
    while (true) {
      const char character = peek();
      // A control character, or the end of the text.
      if (static_cast<unsigned char>(character) < 0x20U) {
        return false;
      }
      take();
      if (character == '"') {
        return true;
      }
      if (character == '\\' && !read_escape()) {
        return false;
      }
    }
  }

  // The rest of an array or an object whose opening bracket is `open`, which
  // makes `depth` of them open.
  constexpr bool read_container(const char open, const std::size_t depth) {
    const char close = open == '[' ? ']' : '}';
    take();
    skip_whitespace();
    if (take_if(close)) {
      return true;
    }
    while (true) {
      if (open == '{') {
        skip_whitespace();
        if (!read_string()) {
          return false;
        }
        skip_whitespace();
        if (!take_if(':')) {
          return false;
        }
      }
      if (!read_value(depth)) {
        return false;
      }
      skip_whitespace();
      if (take_if(close)) {
        return true;
      }
      if (!take_if(',')) {
        return false;
      }
    }
  }

  // One value, with `depth` arrays and objects open around it.
  constexpr bool read_value(const std::size_t depth) {
    skip_whitespace();
    switch (peek()) {
    case '{':
    case '[':
      return depth < max_json_depth && read_container(peek(), depth + 1);
    case '"':
      return read_string();
    case 't':
      return read_word("true");
    case 'f':
      return read_word("false");
    case 'n':
      return read_word("null");
    default:
      return read_number();
    }
  }
};

// `text` without the whitespace between its tokens, when it is exactly one JSON
// value as JsonView::parse() reads one, and otherwise nothing.
constexpr std::optional<std::string> compact_json(const std::string_view text) {
  compact_json_reader reader{.text = text};
  if (!is_utf8(text) || !reader.read_value(0)) {
    return std::nullopt;
  }
  reader.skip_whitespace();
  if (reader.position != text.size()) {
    return std::nullopt;
  }
  return std::move(reader.output);
}

// ---- Annotations -------------------------------------------------------------

// The name a diagnostic gives one of these annotations, or empty for any other.
consteval std::string_view extra_keyword_name(const std::meta::info annotation) {
  if (annotation_matches(annotation, ^^title)) {
    return "scrydantic::title";
  }
  if (annotation_matches(annotation, ^^keyword)) {
    return "scrydantic::keyword";
  }
  if (annotation_matches(annotation, ^^keyword_string)) {
    return "scrydantic::keyword_string";
  }
  return {};
}

// A keyword or a keyword_string: an annotation that names its own keyword.
consteval bool is_custom_keyword(const std::meta::info annotation) {
  return annotation_matches(annotation, ^^keyword) ||
         annotation_matches(annotation, ^^keyword_string);
}

// The two texts of a custom keyword, read as annotation_text_v reads the one
// text of an annotation that has one.
template <auto Annotation>
inline constexpr static_text keyword_key_v{Annotation.key.text,
                                           sizeof(Annotation.key.text) - 1};

template <auto Annotation>
inline constexpr static_text keyword_value_v{Annotation.value.text,
                                             sizeof(Annotation.value.text) - 1};

consteval std::string_view keyword_text(const std::meta::info annotation,
                                        const std::meta::info variable) {
  const auto text = std::meta::extract<static_text>(
      std::meta::substitute(variable, {std::meta::constant_of(annotation)}));
  return {text.data, text.size};
}

consteval std::string_view keyword_key(const std::meta::info annotation) {
  return keyword_text(annotation, ^^keyword_key_v);
}

// The value a custom keyword has in a schema: the JSON of a keyword without the
// whitespace between its tokens, or the text of a keyword_string as a JSON
// string. Nothing for a keyword whose value is not JSON.
consteval std::optional<std::string> keyword_value(const std::meta::info annotation) {
  const auto text = keyword_text(annotation, ^^keyword_value_v);
  if (annotation_matches(annotation, ^^keyword)) {
    return compact_json(text);
  }
  std::string value{};
  append_json_string(value, text);
  return value;
}

// Every keyword that a schema writer writes of its own accord, in some schema.
// A custom keyword may use none of them, whatever schema it would join: what a
// generated keyword says is the codec's promise, which no annotation changes,
// and one fixed list makes that a rule that no member's type bends. A writer
// that learns a new keyword adds it here.
inline constexpr std::array<std::string_view, 22> reserved_keywords{
    "$defs",
    "$ref",
    "additionalProperties",
    "anyOf",
    "default",
    "description",
    "enum",
    "exclusiveMaximum",
    "exclusiveMinimum",
    "items",
    "maxItems",
    "maxLength",
    "maximum",
    "minItems",
    "minLength",
    "minimum",
    "multipleOf",
    "pattern",
    "properties",
    "required",
    "title",
    "type",
};

// Why the schema of `entity`, a data member or a class, cannot state what these
// annotations of it say; empty when it can. The text follows the entity's name.
consteval std::string extra_keywords_problem(const std::meta::info entity) {
  if (annotation_count(entity, ^^title) > 1) {
    return "has more than one scrydantic::title annotation";
  }
  std::vector<std::string_view> keys{};
  for (const auto annotation : std::meta::annotations_of(entity)) {
    if (!is_custom_keyword(annotation)) {
      continue;
    }
    const auto key = keyword_key(annotation);
    const auto shown = "the key \"" + owned_text(key) + "\"";
    if (key.empty()) {
      return "carries a keyword annotation with an empty key";
    }
    if (!keyword_value(annotation)) {
      return "carries a scrydantic::keyword with " + shown +
             " whose value is not one JSON value";
    }
    if (std::ranges::contains(reserved_keywords, key)) {
      return "carries a keyword annotation with " + shown +
             ", which Scrydantic writes itself; an annotation cannot replace a "
             "generated keyword";
    }
    if (std::ranges::contains(keys, key)) {
      return "has more than one keyword annotation with " + shown;
    }
    keys.push_back(key);
  }
  return {};
}

// The keywords that these annotations of `entity` add to its schema, in no
// particular order. Only an entity that has passed extra_keywords_problem gets
// here, so each value is JSON.
consteval std::vector<schema_keyword> extra_keywords(const std::meta::info entity) {
  std::vector<schema_keyword> keywords{};
  for (const auto annotation : std::meta::annotations_of(entity)) {
    if (annotation_matches(annotation, ^^title)) {
      std::string value{};
      append_json_string(value, annotation_text(annotation));
      keywords.push_back({.key = "title", .value = std::move(value)});
    } else if (is_custom_keyword(annotation)) {
      keywords.push_back({.key = owned_text(keyword_key(annotation)),
                          .value = keyword_value(annotation).value()});
    }
  }
  return keywords;
}

} // namespace scrydantic::detail
