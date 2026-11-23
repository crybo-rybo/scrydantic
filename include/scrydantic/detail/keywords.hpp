#pragma once

#include <cstddef>
#include <meta>
#include <scrydantic/annotations.hpp>
#include <scrydantic/detail/constraints.hpp>
#include <scrydantic/detail/json_string.hpp>
#include <scrydantic/detail/model.hpp>
#include <string>
#include <string_view>
#include <vector>

// The annotations that add a keyword to a schema and do nothing else: neither
// encoding nor decoding reads them. A data member or a class carries them. One
// resolution serves the family checks, which reject what cannot be written, and
// the schema generator, which writes the rest.
namespace scrydantic::detail {

// The name a diagnostic gives such an annotation, or empty for any other.
consteval std::string_view extra_keyword_name(const std::meta::info annotation) {
  if (annotation_matches(annotation, ^^title)) {
    return "scrydantic::title";
  }
  return {};
}

// Why the schema of `entity`, a data member or a class, cannot state what these
// annotations of it say; empty when it can. The text follows the entity's name.
consteval std::string extra_keywords_problem(const std::meta::info entity) {
  if (annotation_count(entity, ^^title) > 1) {
    return "has more than one scrydantic::title annotation";
  }
  return {};
}

// The keywords that these annotations of `entity` add to its schema, in no
// particular order.
consteval std::vector<schema_keyword> extra_keywords(const std::meta::info entity) {
  std::vector<schema_keyword> keywords{};
  if (const auto text = annotated_text(entity, ^^title)) {
    std::string value{};
    append_json_string(value, *text);
    keywords.push_back({.key = "title", .value = std::move(value)});
  }
  return keywords;
}

} // namespace scrydantic::detail
