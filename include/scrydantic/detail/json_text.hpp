#pragma once

#include <cstddef>
#include <cstdint>
#include <scrydantic/json.hpp>
#include <string>
#include <string_view>

// The JSON core's text operations that the header-only reflected codec calls
// across the compiled boundary, besides the public is_valid_json(). They are not part
// of the stable API. The document type behind them stays inside src/json/document.hpp,
// which states what is accepted and how canonical text is written; outside the core,
// parsed JSON is read through the public JsonView.
namespace scrydantic::detail {

// The deepest nesting of arrays and objects the parser accepts, counting an
// empty one.
inline constexpr std::size_t json_max_depth = 256;

// What check_json() found: valid JSON, text that stopped being read at a
// container nested deeper than json_max_depth, or text that breaks another rule.
enum class json_validity : std::uint8_t {
  valid,
  too_deep,
  invalid,
};

// is_valid_json() that also says why it rejects text, in the same single pass.
[[nodiscard]] json_validity check_json(std::string_view input) noexcept;

// Why JsonView::parse() rejects `input` and where it stopped reading, as
// `<reason> at line <line>, column <column>`, such as `expected ':' at line 1,
// column 7`: what follows the colon in that rejection's message. The column
// counts code points. Empty when `input` is one JSON document. Its words are a
// fixed phrase and numbers derived from the input, so a model may see it.
[[nodiscard]] std::string describe_invalid_json(std::string_view input);

// Appends the canonical form of `text` to `output`: object keys in lexical
// order, no insignificant whitespace, and the canonical spelling of every number
// and string escape, as JsonView::to_json() writes it. False, with `output`
// unchanged, when the text is not one JSON document.
[[nodiscard]] bool append_canonical_json(std::string& output, std::string_view text);

// format_json_number(), appended to `output` in place: the shortest round-trip
// digits, as std::to_chars writes them, but laid out positionally for decimal
// exponents from -4 to 15 and otherwise as `1E-7`, where std::to_chars chooses by
// length and writes `1e-07`. A non-finite value, which JSON cannot spell, is
// written as null.
void append_canonical_json_number(std::string& output, double value);

} // namespace scrydantic::detail
