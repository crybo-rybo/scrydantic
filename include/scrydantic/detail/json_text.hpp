#pragma once

#include <scrydantic/json.hpp>
#include <string>
#include <string_view>

// The JSON core's text operations that the header-only reflected codec calls
// across the compiled boundary. They are not part of the stable API. The
// document type behind them stays inside src/json/document.hpp, which states what
// is accepted and how canonical text is written; outside the core, parsed JSON is
// read through the public JsonView.
namespace scrydantic::detail {

// Validates JSON text without materializing a document: one allocation-free pass
// that accepts exactly what the parser accepts, rejecting malformed interiors,
// truncation, trailing garbage, and a second document. Surrounding whitespace is
// accepted, as any parser would.
[[nodiscard]] bool validate_json(std::string_view input) noexcept;

// Appends the canonical form of `text` to `output`: object keys in lexical
// order, no insignificant whitespace, and the canonical spelling of every number
// and string escape, as JsonView::to_json() writes it. False, with `output`
// unchanged, when the text is not one JSON document.
[[nodiscard]] bool append_canonical_json(std::string& output, std::string_view text);

// Appends a double as the canonical writer spells it: the shortest round-trip
// digits, as std::to_chars writes them, but laid out positionally for decimal
// exponents from -4 to 15 and otherwise as `1E-7`, where std::to_chars chooses by
// length and writes `1e-07`. A non-finite value, which JSON cannot spell, is
// written as null.
void append_canonical_json_number(std::string& output, double value);

} // namespace scrydantic::detail
