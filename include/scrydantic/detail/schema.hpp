#pragma once

#include <algorithm>
#include <cstdint>
#include <limits>
#include <meta>
#include <optional>
#include <scrydantic/detail/json_string.hpp>
#include <scrydantic/detail/meta.hpp>
#include <scrydantic/detail/model.hpp>
#include <string>
#include <string_view>
#include <vector>

// Schema generation is a consteval function of a type's reflection. It runs only
// for types the SupportedValue check has admitted.
//
// A schema is one tree written in place, as many model APIs handle "$ref" poorly
// or not at all, except where a type contains itself. Each aggregate form that
// can contain itself (definitions_of) is written once under the root's "$defs"
// and referred to elsewhere as {"$ref":"#/$defs/<name>"}. A root that contains
// itself stays the root and refers to itself as {"$ref":"#"}, rather than moving
// into "$defs" behind a root "$ref": the root then keeps its own keywords, such
// as the "type":"object" that tool APIs require of an input schema, and a schema
// whose only recursion is its root needs no "$defs" at all.
namespace scrydantic::detail {

using schema_text = std::optional<std::string_view>;

// The nodes a schema refers to rather than writes in place. A root node is
// written in place once, as the root, so meeting it again means recursion. Each
// definition has a name of its own, which the SupportedValue check has ensured.
struct schema_references {
  std::optional<schema_node> root{};
  std::vector<schema_node> definitions{};
};

consteval void append_unsigned(std::vector<char>& output, const std::uint64_t value) {
  append_json_literal(output, decimal(value));
}

// Schema keys are emitted in sorted order, so "description" comes first in most
// schemas but after the "additionalProperties" of an aggregate and the "anyOf" of an
// optional or a variant. These two helpers cover both placements.

consteval void append_schema_prologue(std::vector<char>& output,
                                      const schema_text description_text) {
  output.push_back('{');
  if (description_text.has_value()) {
    append_json_literal(output, "\"description\":");
    append_json_string(output, *description_text);
    output.push_back(',');
  }
}

consteval void append_description_key(std::vector<char>& output,
                                      const schema_text description_text) {
  if (!description_text.has_value()) {
    return;
  }
  append_json_literal(output, ",\"description\":");
  append_json_string(output, *description_text);
}

consteval void append_schema(std::vector<char>& output, std::meta::info type,
                             schema_text description_text,
                             const schema_references& references);

consteval void append_described_type(std::vector<char>& output,
                                     const schema_text description_text,
                                     const std::string_view type) {
  append_schema_prologue(output, description_text);
  append_json_literal(output, "\"type\":");
  append_json_string(output, type);
  output.push_back('}');
}

consteval void append_integer_schema(std::vector<char>& output,
                                     const std::meta::info type,
                                     const schema_text description_text) {
  const auto bits = std::meta::size_of(type) * 8U;
  const bool is_signed = std::meta::is_signed_type(type);
  const auto magnitude_bits = is_signed ? bits - 1U : bits;
  const auto maximum = magnitude_bits == 64U
                           ? std::numeric_limits<std::uint64_t>::max()
                           : (std::uint64_t{1} << magnitude_bits) - 1U;
  append_schema_prologue(output, description_text);
  append_json_literal(output, "\"maximum\":");
  append_unsigned(output, maximum);
  append_json_literal(output, ",\"minimum\":");
  if (is_signed) {
    // The lowest value is -(maximum + 1), whose magnitude an unsigned holds.
    output.push_back('-');
    append_unsigned(output, maximum + 1U);
  } else {
    output.push_back('0');
  }
  append_json_literal(output, ",\"type\":\"integer\"}");
}

consteval void append_enum_schema(std::vector<char>& output, const std::meta::info type,
                                  const schema_text description_text) {
  append_schema_prologue(output, description_text);
  append_json_literal(output, "\"enum\":[");
  bool first = true;
  for (const auto enumerator : std::meta::enumerators_of(type)) {
    if (!first) {
      output.push_back(',');
    }
    append_json_string(output, std::meta::identifier_of(enumerator));
    first = false;
  }
  append_json_literal(output, "],\"type\":\"string\"}");
}

consteval void append_sequence_schema(std::vector<char>& output,
                                      const std::meta::info type,
                                      const schema_text description_text,
                                      const schema_references& references) {
  append_schema_prologue(output, description_text);
  append_json_literal(output, "\"items\":");
  append_schema(output, element_of(type), std::nullopt, references);
  if (kind_of(type) == value_kind::array) {
    const auto size = array_size_of(type);
    append_json_literal(output, ",\"maxItems\":");
    append_unsigned(output, size);
    append_json_literal(output, ",\"minItems\":");
    append_unsigned(output, size);
  }
  append_json_literal(output, ",\"type\":\"array\"}");
}

// The "type" property a tagged alternative's schema gains.
consteval void append_tag_property(std::vector<char>& output,
                                   const std::string_view tag_text) {
  append_json_string(output, tag_key);
  append_json_literal(output, ":{\"enum\":[");
  append_json_string(output, tag_text);
  append_json_literal(output, "],\"type\":\"string\"}");
}

// Writes the `properties` members, or with `required_only` the `required` names,
// merging the tag key into its sorted position when there is one.
consteval void append_object_keys(std::vector<char>& output, const std::meta::info type,
                                  const schema_text tag_text, const bool required_only,
                                  const schema_references& references) {
  bool first = true;
  bool tag_pending = tag_text.has_value();
  const auto separate = [&output, &first] {
    if (!first) {
      output.push_back(',');
    }
    first = false;
  };
  const auto flush_tag = [&](const bool before) {
    if (tag_pending && before) {
      separate();
      if (required_only) {
        append_json_string(output, tag_key);
      } else {
        append_tag_property(output, *tag_text);
      }
      tag_pending = false;
    }
  };
  for (const auto& field : fields_of(type)) {
    flush_tag(field.key_view() > tag_key);
    if (required_only && !field.required) {
      continue;
    }
    separate();
    append_json_string(output, field.key_view());
    if (!required_only) {
      output.push_back(':');
      append_schema(output, std::meta::type_of(field.member),
                    annotated_text(field.member, ^^description), references);
    }
  }
  flush_tag(true);
}

// An aggregate's own schema, which a definition and the root write in place.
consteval void append_object_schema(std::vector<char>& output, const schema_node node,
                                    const schema_text description_text,
                                    const schema_references& references) {
  const auto tag_text = node.tagged ? tag_of(node.type) : schema_text{};
  append_json_literal(output, "{\"additionalProperties\":false");
  append_description_key(output, description_text);
  append_json_literal(output, ",\"properties\":{");
  append_object_keys(output, node.type, tag_text, false, references);
  append_json_literal(output, "},\"required\":[");
  append_object_keys(output, node.type, tag_text, true, references);
  append_json_literal(output, "],\"type\":\"object\"}");
}

// "$ref" sorts before "description". JSON Schema 2019-09 and later apply a
// keyword beside "$ref"; draft-07 ignores it, which loses the annotation alone,
// since a reference carries no other keyword. Wrapping the reference in "allOf"
// would keep the description for draft-07 too, at the cost of a keyword that
// model APIs support less widely than "$ref".
consteval void append_reference(std::vector<char>& output,
                                const std::string_view target,
                                const schema_text description_text) {
  append_json_literal(output, "{\"$ref\":");
  append_json_string(output, target);
  append_description_key(output, description_text);
  output.push_back('}');
}

consteval void append_node(std::vector<char>& output, const schema_node node,
                           const schema_text description_text,
                           const schema_references& references) {
  if (references.root == node) {
    return append_reference(output, "#", description_text);
  }
  if (std::ranges::contains(references.definitions, node)) {
    return append_reference(output, "#/$defs/" + definition_name(node).value(),
                            description_text);
  }
  append_object_schema(output, node, description_text, references);
}

consteval void append_alternatives(std::vector<char>& output,
                                   const std::meta::info variant,
                                   const schema_references& references) {
  bool first = true;
  for (const auto alternative : alternatives_of(variant)) {
    if (!first) {
      output.push_back(',');
    }
    append_node(output, {.type = plain_type(alternative), .tagged = true}, std::nullopt,
                references);
    first = false;
  }
}

// An optional variant joins null to the variant's own anyOf rather than nesting one
// anyOf inside another.
consteval void append_optional_schema(std::vector<char>& output,
                                      const std::meta::info type,
                                      const schema_text description_text,
                                      const schema_references& references) {
  const auto element = owned_type(element_of(type));
  append_json_literal(output, "{\"anyOf\":[");
  if (kind_of(element) == value_kind::variant) {
    append_alternatives(output, element, references);
  } else {
    append_schema(output, element, std::nullopt, references);
  }
  append_json_literal(output, ",{\"type\":\"null\"}]");
  append_description_key(output, description_text);
  output.push_back('}');
}

consteval void append_variant_schema(std::vector<char>& output,
                                     const std::meta::info type,
                                     const schema_text description_text,
                                     const schema_references& references) {
  append_json_literal(output, "{\"anyOf\":[");
  append_alternatives(output, type, references);
  output.push_back(']');
  append_description_key(output, description_text);
  output.push_back('}');
}

consteval void append_schema(std::vector<char>& output, const std::meta::info reflected,
                             const schema_text description_text,
                             const schema_references& references) {
  const auto type = plain_type(reflected);
  switch (kind_of(type)) {
  case value_kind::boolean:
    return append_described_type(output, description_text, "boolean");
  case value_kind::integer:
    return append_integer_schema(output, type, description_text);
  case value_kind::floating:
    return append_described_type(output, description_text, "number");
  case value_kind::string:
    return append_described_type(output, description_text, "string");
  case value_kind::enumeration:
    return append_enum_schema(output, type, description_text);
  case value_kind::optional:
    return append_optional_schema(output, type, description_text, references);
  case value_kind::indirect:
    return append_schema(output, element_of(type), description_text, references);
  case value_kind::vector:
  case value_kind::array:
    return append_sequence_schema(output, type, description_text, references);
  case value_kind::variant:
    return append_variant_schema(output, type, description_text, references);
  case value_kind::aggregate:
    return append_node(output, {.type = type, .tagged = false}, description_text,
                       references);
  default:
    // Unreachable: the SupportedValue check admits no other kind.
    throw std::meta::exception(u8"schema generation reached a type "
                               u8"outside SupportedValue",
                               type);
  }
}

// "$defs" sorts before every keyword a root schema writes, so it opens the root.
consteval void append_definitions(std::vector<char>& output,
                                  const schema_references& references) {
  append_json_literal(output, "{\"$defs\":{");
  bool first = true;
  for (const auto node : references.definitions) {
    if (!first) {
      output.push_back(',');
    }
    append_json_string(output, definition_name(node).value());
    output.push_back(':');
    append_object_schema(output, node, std::nullopt, references);
    first = false;
  }
  append_json_literal(output, "},");
}

consteval std::string_view static_schema(const std::vector<char>& text) {
  return {std::define_static_string(text), text.size()};
}

consteval std::string_view make_schema(const std::meta::info type) {
  const schema_references references{
      .root = root_node(type),
      .definitions = definitions_of(type),
  };
  std::vector<char> root{};
  if (references.root.has_value()) {
    append_object_schema(root, *references.root, std::nullopt, references);
  } else {
    append_schema(root, type, std::nullopt, references);
  }
  if (references.definitions.empty()) {
    return static_schema(root);
  }
  // The root's own keywords follow "$defs" inside the root's one pair of braces.
  std::vector<char> output{};
  append_definitions(output, references);
  output.insert(output.end(), root.begin() + 1, root.end());
  return static_schema(output);
}

template <typename Value> consteval std::string_view checked_schema() {
  constexpr std::string_view problem = supported_diagnostic<Value>();
  static_assert(problem.empty(), problem);
  if constexpr (problem.empty()) {
    return make_schema(^^Value);
  } else {
    return {};
  }
}

template <typename Args> consteval std::string_view checked_input_schema() {
  constexpr std::string_view problem = tool_arguments_diagnostic<Args>();
  static_assert(problem.empty(), problem);
  if constexpr (problem.empty()) {
    return make_schema(^^Args);
  } else {
    return {};
  }
}

} // namespace scrydantic::detail

namespace scrydantic {

/// Canonical JSON Schema generated for any supported reflected value.
///
/// Handler result types are supported roots too. A tool binding never sends a
/// result schema to a model; it is here so a host can export its own tool
/// contracts. A type outside
/// SupportedValue fails to compile with the offending member path and the reason.
/// @tparam Value Type satisfying SupportedValue.
template <typename Value>
inline constexpr std::string_view schema_v = detail::checked_schema<Value>();

/// Canonical JSON Schema generated for a reflected argument aggregate.
/// @tparam Args Type satisfying ToolArguments.
template <typename Args>
inline constexpr std::string_view input_schema_v = detail::checked_input_schema<Args>();

} // namespace scrydantic
