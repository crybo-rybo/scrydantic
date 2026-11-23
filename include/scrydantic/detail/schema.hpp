#pragma once

#include <algorithm>
#include <meta>
#include <optional>
#include <scrydantic/detail/constraints.hpp>
#include <scrydantic/detail/defaults.hpp>
#include <scrydantic/detail/json_number.hpp>
#include <scrydantic/detail/json_string.hpp>
#include <scrydantic/detail/keywords.hpp>
#include <scrydantic/detail/meta.hpp>
#include <scrydantic/detail/model.hpp>
#include <string>
#include <string_view>
#include <utility>
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

using schema_keywords = std::vector<schema_keyword>;

// The nodes a schema refers to rather than writes in place. A root node is
// written in place once, as the root, so meeting it again means recursion. Each
// definition has a name of its own, which the SupportedValue check has ensured.
struct schema_references {
  std::optional<schema_node> root{};
  std::vector<schema_node> definitions{};
};

// Writes one schema, `{"key":value,...}`, with its keywords in the sorted order
// that canonical JSON has. Every writer below gathers the keywords of its own
// shape with those its caller hands it and ends here, so where a keyword stands
// never depends on which others a schema happens to have. No two of them share
// a key: a writer's own differ, and a custom keyword may use none of them
// (reserved_keywords).
consteval void append_schema_object(std::vector<char>& output,
                                    schema_keywords keywords) {
  std::ranges::sort(keywords, {}, &schema_keyword::key);
  if (std::ranges::adjacent_find(keywords, {}, &schema_keyword::key) !=
      keywords.end()) {
    // Unreachable while reserved_keywords lists every keyword a writer adds.
    throw std::meta::exception(u8"schema generation wrote one keyword twice", ^^::);
  }
  output.push_back('{');
  bool first = true;
  for (const auto& keyword : keywords) {
    if (!first) {
      output.push_back(',');
    }
    append_json_string(output, keyword.key);
    output.push_back(':');
    append_json_literal(output, keyword.value);
    first = false;
  }
  output.push_back('}');
}

// `text` as a JSON string: the value of a keyword such as "type".
consteval std::string string_value(const std::string_view text) {
  std::string literal{};
  append_json_string(literal, text);
  return literal;
}

// The keyword whose value is the JSON that `write` appends.
template <typename Write>
consteval schema_keyword written_keyword(std::string key, const Write& write) {
  std::vector<char> value{};
  write(value);
  return {.key = std::move(key), .value = std::string(value.begin(), value.end())};
}

consteval void append_schema(std::vector<char>& output, std::meta::info type,
                             const schema_keywords& member,
                             const schema_keywords& value,
                             const schema_references& references);

consteval void append_typed_schema(std::vector<char>& output,
                                   const std::string_view type,
                                   schema_keywords keywords) {
  keywords.push_back({.key = "type", .value = string_value(type)});
  append_schema_object(output, std::move(keywords));
}

// An integer has one lower and one upper bound: the constraints' where they set
// one, which a valid constraint keeps within the type, and the type's own where
// they do not.
consteval void append_integer_schema(std::vector<char>& output,
                                     const std::meta::info type,
                                     schema_keywords keywords) {
  const auto bounded = [&keywords](const std::string_view inclusive,
                                   const std::string_view exclusive) {
    return std::ranges::any_of(keywords, [&](const schema_keyword& keyword) {
      return keyword.key == inclusive || keyword.key == exclusive;
    });
  };
  const auto numeric = numeric_type_of(type);
  if (!bounded("minimum", "exclusiveMinimum")) {
    keywords.push_back(
        {.key = "minimum",
         .value = bound_text(integer_bound(true, numeric.lowest_magnitude))});
  }
  if (!bounded("maximum", "exclusiveMaximum")) {
    keywords.push_back({.key = "maximum", .value = decimal(numeric.maximum)});
  }
  append_typed_schema(output, "integer", std::move(keywords));
}

// The value of an enum's "enum" keyword: the names of its enumerators.
consteval void append_enumerator_names(std::vector<char>& output,
                                       const std::meta::info type) {
  output.push_back('[');
  bool first = true;
  for (const auto enumerator : std::meta::enumerators_of(type)) {
    if (!first) {
      output.push_back(',');
    }
    append_json_string(output, std::meta::identifier_of(enumerator));
    first = false;
  }
  output.push_back(']');
}

consteval void append_enum_schema(std::vector<char>& output, const std::meta::info type,
                                  schema_keywords keywords) {
  keywords.push_back(written_keyword("enum", [type](std::vector<char>& names) {
    append_enumerator_names(names, type);
  }));
  append_typed_schema(output, "string", std::move(keywords));
}

// A fixed-size array's size is its only item bound; the checks admit no items
// constraint on one. The element's schema may be a "$ref"; the item bounds sit
// beside "items", not inside it.
consteval void append_sequence_schema(std::vector<char>& output,
                                      const std::meta::info type,
                                      schema_keywords keywords,
                                      const schema_references& references) {
  keywords.push_back(written_keyword("items", [&](std::vector<char>& items) {
    append_schema(items, element_of(type), {}, {}, references);
  }));
  if (kind_of(type) == value_kind::array) {
    const auto size = decimal(array_size_of(type));
    keywords.push_back({.key = "maxItems", .value = size});
    keywords.push_back({.key = "minItems", .value = size});
  }
  append_typed_schema(output, "array", std::move(keywords));
}

// The "type" property a tagged alternative's schema gains.
consteval void append_tag_property(std::vector<char>& output,
                                   const std::string_view tag_text) {
  append_json_string(output, tag_key);
  append_json_literal(output, ":{\"enum\":[");
  append_json_string(output, tag_text);
  append_json_literal(output, "],\"type\":\"string\"}");
}

// What a member's schema states about the member, whatever value it holds: its
// description, and the default it has when its key is absent. An optional
// member's schema states them beside the "anyOf", where null is a value too, and
// a reference beside the "$ref".
//
// A member has a default when decoding leaves it as its initializer set it: it
// has an initializer, and skip_null does not cover it, since the absence of a
// member that skip_null covers decodes as disengaged whatever its initializer.
// The default is the program's own value, so nothing holds it to the member's
// constraints, as nothing holds an absent member to them when decoding.
consteval schema_keywords member_keywords(const field_layout& field) {
  schema_keywords keywords{};
  if (const auto text = annotated_text(field.member, ^^description)) {
    keywords.push_back({.key = "description", .value = string_value(*text)});
  }
  if (!field.required && !field.omit_null) {
    if (const auto text = default_of(field.member); !text.empty()) {
      keywords.push_back({.key = "default", .value = owned_text(text)});
    }
  }
  return keywords;
}

// What a member's schema states about the value it holds: the keywords of its
// constraints, its title, and its custom keywords. An optional member's schema
// states them of the engaged value, inside the "anyOf".
consteval schema_keywords value_keywords(const std::meta::info member) {
  auto keywords = constraint_keywords(member);
  keywords.append_range(extra_keywords(member));
  return keywords;
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
      append_schema(output, std::meta::type_of(field.member), member_keywords(field),
                    value_keywords(field.member), references);
    }
  }
  flush_tag(true);
}

// An aggregate's own schema, which a definition and the root write in place.
// The class's title and custom keywords stand on it wherever it is written, but
// for one that `keywords`, a member's, already state: what a member says of its
// own value is the more specific.
consteval void append_object_schema(std::vector<char>& output, const schema_node node,
                                    schema_keywords keywords,
                                    const schema_references& references) {
  const auto tag_text = node.tagged ? tag_of(node.type) : schema_text{};
  for (auto& keyword : extra_keywords(node.type)) {
    if (!std::ranges::contains(keywords, keyword.key, &schema_keyword::key)) {
      keywords.push_back(std::move(keyword));
    }
  }
  keywords.push_back({.key = "additionalProperties", .value = "false"});
  keywords.push_back(written_keyword("properties", [&](std::vector<char>& properties) {
    properties.push_back('{');
    append_object_keys(properties, node.type, tag_text, false, references);
    properties.push_back('}');
  }));
  keywords.push_back(written_keyword("required", [&](std::vector<char>& required) {
    required.push_back('[');
    append_object_keys(required, node.type, tag_text, true, references);
    required.push_back(']');
  }));
  append_typed_schema(output, "object", std::move(keywords));
}

// A member's keywords stand beside the "$ref" to its type. JSON Schema 2019-09
// and later apply a keyword beside "$ref"; draft-07 ignores it, which loses the
// annotation alone, since a reference carries no other keyword. Wrapping the
// reference in "allOf" would keep a description for draft-07 too, at the cost of
// a keyword that model APIs support less widely than "$ref".
consteval void append_reference(std::vector<char>& output,
                                const std::string_view target,
                                schema_keywords keywords) {
  keywords.push_back({.key = "$ref", .value = string_value(target)});
  append_schema_object(output, std::move(keywords));
}

consteval void append_node(std::vector<char>& output, const schema_node node,
                           schema_keywords keywords,
                           const schema_references& references) {
  if (references.root == node) {
    return append_reference(output, "#", std::move(keywords));
  }
  if (std::ranges::contains(references.definitions, node)) {
    return append_reference(output, "#/$defs/" + definition_name(node).value(),
                            std::move(keywords));
  }
  append_object_schema(output, node, std::move(keywords), references);
}

consteval void append_alternatives(std::vector<char>& output,
                                   const std::meta::info variant,
                                   const schema_references& references) {
  bool first = true;
  for (const auto alternative : alternatives_of(variant)) {
    if (!first) {
      output.push_back(',');
    }
    append_node(output, {.type = plain_type(alternative), .tagged = true}, {},
                references);
    first = false;
  }
}

// An optional variant joins null to the variant's own anyOf rather than nesting one
// anyOf inside another. Constraints bound the engaged value, so their keywords go
// to its schema, and null stays allowed. So do a title and a custom keyword, but
// for an optional variant's, which have no schema apart from the joined anyOf to
// go to.
consteval void append_optional_schema(std::vector<char>& output,
                                      const std::meta::info type,
                                      schema_keywords member,
                                      const schema_keywords& value,
                                      const schema_references& references) {
  const auto element = owned_type(element_of(type));
  member.push_back(written_keyword("anyOf", [&](std::vector<char>& schemas) {
    schemas.push_back('[');
    if (kind_of(element) == value_kind::variant) {
      append_alternatives(schemas, element, references);
    } else {
      append_schema(schemas, element, {}, value, references);
    }
    append_json_literal(schemas, ",{\"type\":\"null\"}]");
  }));
  if (kind_of(element) == value_kind::variant) {
    member.append_range(value);
  }
  append_schema_object(output, std::move(member));
}

consteval void append_variant_schema(std::vector<char>& output,
                                     const std::meta::info type,
                                     schema_keywords keywords,
                                     const schema_references& references) {
  keywords.push_back(written_keyword("anyOf", [&](std::vector<char>& schemas) {
    schemas.push_back('[');
    append_alternatives(schemas, type, references);
    schemas.push_back(']');
  }));
  append_schema_object(output, std::move(keywords));
}

// `member` is what the schema states about a member whatever it holds, and
// `value` what it states about the value held. Only an optional tells the two
// apart, stating `member` beside its "anyOf" and `value` inside it. An indirect
// is the value it owns, so both go to that value's schema.
consteval void append_schema(std::vector<char>& output, const std::meta::info reflected,
                             const schema_keywords& member,
                             const schema_keywords& value,
                             const schema_references& references) {
  const auto type = plain_type(reflected);
  auto keywords = member;
  keywords.append_range(value);
  switch (kind_of(type)) {
  case value_kind::boolean:
    return append_typed_schema(output, "boolean", std::move(keywords));
  case value_kind::integer:
    return append_integer_schema(output, type, std::move(keywords));
  case value_kind::floating:
    return append_typed_schema(output, "number", std::move(keywords));
  case value_kind::string:
    return append_typed_schema(output, "string", std::move(keywords));
  case value_kind::enumeration:
    return append_enum_schema(output, type, std::move(keywords));
  case value_kind::optional:
    return append_optional_schema(output, type, member, value, references);
  case value_kind::indirect:
    return append_schema(output, element_of(type), member, value, references);
  case value_kind::vector:
  case value_kind::array:
    return append_sequence_schema(output, type, std::move(keywords), references);
  case value_kind::variant:
    return append_variant_schema(output, type, std::move(keywords), references);
  case value_kind::aggregate:
    return append_node(output, {.type = type, .tagged = false}, std::move(keywords),
                       references);
  default:
    // Unreachable: the SupportedValue check admits no other kind.
    throw std::meta::exception(u8"schema generation reached a type "
                               u8"outside SupportedValue",
                               type);
  }
}

// The value of the root's "$defs": each definition's schema under its name.
consteval void append_definitions(std::vector<char>& output,
                                  const schema_references& references) {
  output.push_back('{');
  bool first = true;
  for (const auto node : references.definitions) {
    if (!first) {
      output.push_back(',');
    }
    append_json_string(output, definition_name(node).value());
    output.push_back(':');
    append_object_schema(output, node, {}, references);
    first = false;
  }
  output.push_back('}');
}

consteval std::string_view static_schema(const std::vector<char>& text) {
  return {std::define_static_string(text), text.size()};
}

// "$defs" is one more keyword of the root's schema, whatever shape the root has.
consteval std::string_view make_schema(const std::meta::info type) {
  const schema_references references{
      .root = root_node(type),
      .definitions = definitions_of(type),
  };
  schema_keywords keywords{};
  if (!references.definitions.empty()) {
    keywords.push_back(written_keyword("$defs", [&](std::vector<char>& definitions) {
      append_definitions(definitions, references);
    }));
  }
  std::vector<char> output{};
  if (references.root.has_value()) {
    append_object_schema(output, *references.root, std::move(keywords), references);
  } else {
    append_schema(output, type, keywords, {}, references);
  }
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
