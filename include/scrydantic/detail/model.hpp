#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <memory>
#include <meta>
#include <optional>
#include <scrydantic/annotations.hpp>
#include <scrydantic/detail/json_string.hpp>
#include <scrydantic/json.hpp>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

// The reflected type model shared by the family checks, the codec, and the schema
// generator. Everything here is a consteval function of std::meta::info, so a
// question such as "which JSON key does this member use" has one answer no matter
// which of those three asks it.
namespace scrydantic::detail {

// The key under which a tagged variant alternative carries its discriminator.
inline constexpr std::string_view tag_key = "type";

// The closed set of shapes the codec knows. Leaves come first, so a check can split
// them from the composites with one comparison.
enum class value_kind : std::uint8_t {
  unsupported,
  boolean,
  integer,
  floating,
  string,
  string_view,
  json,
  enumeration,
  optional,
  indirect,
  vector,
  array,
  variant,
  aggregate,
};

// Strips aliases, references, and cv-qualifiers so reflections compare by identity.
consteval std::meta::info plain_type(const std::meta::info type) {
  return std::meta::dealias(std::meta::remove_cvref(std::meta::dealias(type)));
}

// ^^Template<Args> names a specialization without instantiating it, and GCC's
// class queries answer for the uninstantiated class until something needs it to
// be complete. size_of does, so it runs first; for an incomplete type it throws.
consteval bool is_complete_class(const std::meta::info type) {
  try {
    static_cast<void>(std::meta::size_of(type));
  } catch (const std::meta::exception&) {
    return false;
  }
  return std::meta::is_complete_type(type);
}

consteval bool is_specialization_of(const std::meta::info type,
                                    const std::meta::info template_reflection) {
  return std::meta::has_template_arguments(type) &&
         std::meta::template_of(type) == template_reflection;
}

consteval bool is_character_type(const std::meta::info type) {
  // Each reflection is parenthesized: ^^char && ... would parse as ^^(char&&).
  for (const auto character : {(^^char), (^^signed char), (^^unsigned char),
                               (^^wchar_t), (^^char8_t), (^^char16_t), (^^char32_t)}) {
    if (type == character) {
      return true;
    }
  }
  return false;
}

consteval value_kind template_kind_of(const std::meta::info type) {
  if (is_specialization_of(type, ^^std::optional)) {
    return value_kind::optional;
  }
  if (is_specialization_of(type, ^^std::indirect)) {
    return value_kind::indirect;
  }
  if (is_specialization_of(type, ^^std::vector)) {
    return value_kind::vector;
  }
  if (is_specialization_of(type, ^^std::array)) {
    return value_kind::array;
  }
  if (is_specialization_of(type, ^^std::variant)) {
    return value_kind::variant;
  }
  if (std::meta::is_class_type(type) && is_complete_class(type) &&
      std::meta::is_aggregate_type(type)) {
    return value_kind::aggregate;
  }
  return value_kind::unsupported;
}

// Classifies a type by shape alone. Whether the shape is admitted, and for which
// family, is the checker's decision, not this function's.
consteval value_kind kind_of(const std::meta::info reflected) {
  const auto type = plain_type(reflected);
  if (type == (^^bool)) {
    return value_kind::boolean;
  }
  if (std::meta::is_integral_type(type)) {
    return is_character_type(type) || std::meta::size_of(type) > sizeof(std::uint64_t)
               ? value_kind::unsupported
               : value_kind::integer;
  }
  if (type == (^^float) || type == (^^double)) {
    return value_kind::floating;
  }
  if (type == plain_type(^^std::string)) {
    return value_kind::string;
  }
  if (type == plain_type(^^std::string_view)) {
    return value_kind::string_view;
  }
  if (type == (^^scrydantic::Json)) {
    return value_kind::json;
  }
  if (std::meta::is_enum_type(type)) {
    return value_kind::enumeration;
  }
  return template_kind_of(type);
}

consteval std::vector<std::meta::info> data_members_of(const std::meta::info type) {
  return std::meta::nonstatic_data_members_of(type,
                                              std::meta::access_context::unchecked());
}

// The first template argument of an optional, std::indirect, vector, or array.
consteval std::meta::info element_of(const std::meta::info container) {
  return std::meta::template_arguments_of(plain_type(container))[0];
}

consteval std::size_t array_size_of(const std::meta::info array) {
  return std::meta::extract<std::size_t>(
      std::meta::template_arguments_of(plain_type(array))[1]);
}

// The value a type stands for once every std::indirect around it is seen
// through: the codec and the schemas treat an indirect as the value it owns.
consteval std::meta::info owned_type(const std::meta::info type) {
  auto owned = plain_type(type);
  while (kind_of(owned) == value_kind::indirect) {
    owned = plain_type(element_of(owned));
  }
  return owned;
}

consteval std::vector<std::meta::info> alternatives_of(const std::meta::info variant) {
  return std::meta::template_arguments_of(plain_type(variant));
}

// ---- Annotations -------------------------------------------------------------

// `what` reflects either an annotation type (skip_null_t) or an annotation class
// template (name), whose every specialization matches.
consteval bool annotation_matches(const std::meta::info annotation,
                                  const std::meta::info what) {
  const auto type = plain_type(std::meta::type_of(annotation));
  return std::meta::is_template(what) ? is_specialization_of(type, what) : type == what;
}

consteval std::size_t annotation_count(const std::meta::info entity,
                                       const std::meta::info what) {
  std::size_t count = 0;
  for (const auto annotation : std::meta::annotations_of(entity)) {
    if (annotation_matches(annotation, what)) {
      ++count;
    }
  }
  return count;
}

consteval bool has_annotation(const std::meta::info entity,
                              const std::meta::info what) {
  return annotation_count(entity, what) != 0;
}

// A fixed-string annotation's text, recovered without naming its extent: the
// annotation value becomes the template argument of a variable template that
// points at the text of that very template-parameter object, so the view has
// static storage duration. std::string_view itself is not structural, which is
// why the variable holds a pointer and a size.
struct static_text {
  const char* data{};
  std::size_t size{};
};

template <auto Annotation>
inline constexpr static_text annotation_text_v{Annotation.text,
                                               sizeof(Annotation.text) - 1};

consteval std::string_view annotation_text(const std::meta::info annotation) {
  // Parenthesized so clang-format, which does not know `^^`, lays it out.
  const auto variable = (^^annotation_text_v);
  const auto text = std::meta::extract<static_text>(
      std::meta::substitute(variable, {std::meta::constant_of(annotation)}));
  return {text.data, text.size};
}

consteval std::optional<std::string_view>
annotated_text(const std::meta::info entity,
               const std::meta::info template_reflection) {
  for (const auto annotation : std::meta::annotations_of(entity)) {
    if (annotation_matches(annotation, template_reflection)) {
      return annotation_text(annotation);
    }
  }
  return std::nullopt;
}

// The JSON key a data member is written under: its name annotation, or else its
// identifier.
consteval std::string_view json_key_of(const std::meta::info member) {
  if (const auto key = annotated_text(member, ^^name)) {
    return *key;
  }
  return std::meta::identifier_of(member);
}

consteval std::optional<std::string_view> tag_of(const std::meta::info type) {
  return annotated_text(plain_type(type), ^^tag);
}

consteval bool ignores_unknown(const std::meta::info type) {
  return has_annotation(plain_type(type), ^^ignore_unknown_t);
}

consteval bool omits_null(const std::meta::info owner, const std::meta::info member) {
  if (kind_of(std::meta::type_of(member)) != value_kind::optional) {
    return false;
  }
  if (has_annotation(member, ^^skip_null_t)) {
    return true;
  }
  return !has_annotation(member, ^^emit_null_t) && has_annotation(owner, ^^skip_null_t);
}

// ---- Field layout ------------------------------------------------------------

// Everything the codec and the schema generator need to know about one data
// member, resolved once. It is a structural type so a class's fields can be one
// static array that `template for` walks; string members point at static strings
// because std::string_view is not structural.
struct field_layout {
  std::meta::info member{};
  const char* key{};
  std::size_t key_size{};
  // Encoding omits the member while it is a disengaged optional, and decoding
  // reads its absence as disengaged.
  bool omit_null{};
  // Decoding rejects an input without the member.
  bool required{};

  [[nodiscard]] consteval std::string_view key_view() const noexcept {
    return {key, key_size};
  }
};

consteval field_layout field_of(const std::meta::info owner,
                                const std::meta::info member) {
  const auto key = json_key_of(member);
  const bool omit = omits_null(owner, member);
  return field_layout{
      .member = member,
      .key = std::define_static_string(key),
      .key_size = key.size(),
      .omit_null = omit,
      .required = !omit && !std::meta::has_default_member_initializer(member),
  };
}

// A class's fields in lexical order of their final JSON keys, which is the
// canonical order objects are written in.
consteval std::vector<field_layout> fields_of(const std::meta::info type) {
  std::vector<field_layout> fields{};
  for (const auto member : data_members_of(plain_type(type))) {
    fields.push_back(field_of(plain_type(type), member));
  }
  std::ranges::sort(fields, {},
                    [](const field_layout& field) { return field.key_view(); });
  return fields;
}

template <typename Type>
inline constexpr auto fields_v = std::define_static_array(fields_of(^^Type));

// Decoding visits members in declaration order, which is the order it reports
// their issues in. This is an array of member reflections rather than a second
// array of field_layout: GCC 16 rejects a define_static_array of consteval-only
// class elements whose value equals one it already made, which the two orders of
// a one-member class would be.
template <typename Type>
inline constexpr auto declared_members_v =
    std::define_static_array(data_members_of(plain_type(^^Type)));

template <typename Variant>
inline constexpr auto alternatives_v =
    std::define_static_array(alternatives_of(^^Variant));

template <typename Enum>
inline constexpr auto enumerators_v =
    std::define_static_array(std::meta::enumerators_of(^^Enum));

// ---- Static text -------------------------------------------------------------

// Copies static text one character at a time. Under AddressSanitizer GCC 16
// cannot constant-fold the null checks std::string's pointer-taking constructor
// and append make on reflected static strings (identifiers, display strings,
// define_static_string arrays), so consteval code never hands them one.
consteval std::string owned_text(const std::string_view text) {
  std::string copy{};
  for (const char character : text) {
    copy.push_back(character);
  }
  return copy;
}

consteval std::string display(const std::meta::info reflection) {
  return owned_text(std::meta::display_string_of(reflection));
}

consteval std::string_view static_string(const std::string& text) {
  return {std::define_static_string(text), text.size()};
}

// The decimal digits of `value`.
consteval std::string decimal(std::uint64_t value) {
  std::string digits{};
  do {
    digits.push_back(static_cast<char>('0' + (value % 10U)));
    value /= 10U;
  } while (value != 0U);
  std::ranges::reverse(digits);
  return digits;
}

// `text` as a JSON string literal, escaped once at compile time so encoding it
// appends one static string.
consteval std::string_view quoted(const std::string_view text) {
  std::string literal{};
  append_json_string(literal, text);
  return static_string(literal);
}

// `"key":`, the text that introduces an object member.
consteval std::string_view member_prefix(const std::string_view key) {
  std::string text{};
  append_json_string(text, key);
  text.push_back(':');
  return static_string(text);
}

// `"type":"<tag>"`, the member a tagged alternative's object gains.
consteval std::string_view tag_member(const std::meta::info alternative) {
  std::string text{};
  append_json_string(text, tag_key);
  text.push_back(':');
  append_json_string(text, *tag_of(alternative));
  return static_string(text);
}

consteval std::string_view joined_names(const std::vector<std::string_view>& names) {
  std::string text{};
  for (const auto entry : names) {
    if (!text.empty()) {
      text.append(", ");
    }
    text += owned_text(entry);
  }
  return static_string(text);
}

// The declared enumerator names, as the decode diagnostic lists them.
consteval std::string_view enumerator_names(const std::meta::info type) {
  std::vector<std::string_view> names{};
  for (const auto enumerator : std::meta::enumerators_of(plain_type(type))) {
    names.push_back(std::meta::identifier_of(enumerator));
  }
  return joined_names(names);
}

// The declared tags of a variant's alternatives, in declaration order.
consteval std::string_view tag_names(const std::meta::info variant) {
  std::vector<std::string_view> names{};
  for (const auto alternative : alternatives_of(variant)) {
    names.push_back(*tag_of(alternative));
  }
  return joined_names(names);
}

// ---- Recursion ---------------------------------------------------------------

// An aggregate as a schema writes it: as a plain value, or as a variant
// alternative, whose object also carries the tag. The two forms have different
// schemas, so whether a form can contain itself is asked of each separately.
struct schema_node {
  std::meta::info type{};
  bool tagged{};

  bool operator==(const schema_node&) const = default;
};

// Appends the aggregates that a value of `type` holds with no aggregate in
// between: the value itself, or what its containers and alternatives hold.
consteval void append_held_nodes(std::vector<schema_node>& nodes,
                                 const std::meta::info type) {
  const auto plain = plain_type(type);
  switch (kind_of(plain)) {
  case value_kind::optional:
  case value_kind::indirect:
  case value_kind::vector:
  case value_kind::array:
    return append_held_nodes(nodes, element_of(plain));
  case value_kind::variant:
    for (const auto alternative : alternatives_of(plain)) {
      nodes.push_back({.type = plain_type(alternative), .tagged = true});
    }
    return;
  case value_kind::aggregate:
    nodes.push_back({.type = plain, .tagged = false});
    return;
  default:
    return;
  }
}

consteval std::vector<schema_node> links_of(const schema_node node) {
  std::vector<schema_node> links{};
  for (const auto member : data_members_of(node.type)) {
    append_held_nodes(links, std::meta::type_of(member));
  }
  return links;
}

// The nodes in `pending` and every node reachable from them through links.
consteval std::vector<schema_node> reachable_from(std::vector<schema_node> pending) {
  std::vector<schema_node> reached{};
  while (!pending.empty()) {
    const auto node = pending.back();
    pending.pop_back();
    if (!std::ranges::contains(reached, node)) {
      reached.push_back(node);
      pending.append_range(links_of(node));
    }
  }
  return reached;
}

consteval bool is_recursive(const schema_node node) {
  return std::ranges::contains(reachable_from(links_of(node)), node);
}

// The node that a schema of `type` is itself, when `type` is an aggregate.
consteval std::optional<schema_node> root_node(const std::meta::info type) {
  const auto owned = owned_type(type);
  if (kind_of(owned) != value_kind::aggregate) {
    return std::nullopt;
  }
  return schema_node{.type = owned, .tagged = false};
}

// ---- Definition names --------------------------------------------------------
//
// A definition's key under $defs, which every "$ref" to it ends with, is spelled
// from reflection, so it neither depends on how the compiler prints a type nor
// shows a model the standard library's internal names:
//
//   definition = class [ "-untagged" ]
//   class      = { scope "." } identifier { "-" argument }
//   scope      = namespace-identifier | "anonymous" | class
//   argument   = type | constant
//   type       = "bool" | "char" | "wchar_t" | "char8_t" | "char16_t" | "char32_t"
//              | ( "int" | "uint" ) width | "float" | "double" | "string"
//              | ( "optional" | "vector" | "indirect" ) "-" type
//              | "array" "-" type "-" constant | "variant" "-" type { "-" type }
//              | class
//   constant   = [ "minus" ] decimal | "true" | "false" | enumerator-identifier
//
// A scope is a namespace, an anonymous namespace, or a class; a class argument
// is an aggregate or an enum; `width` is an integer's width in bits, so a name is
// the same on every platform; and a vector or an indirect uses std::allocator.
// Thus recursive::Node<std::string> is "recursive.Node-string", Spin<-1> in an
// anonymous namespace is "anonymous.Spin-minus1", and the plain form of a class
// that carries a tag, whose schema lacks the tag, is "<class>-untagged".
//
// Every identifier must be ASCII letters, digits, and '_', so a name holds only
// [A-Za-z0-9_.-]: a URI fragment and a JSON Pointer carry those unescaped, and
// OpenAPI accepts them as a component name. Percent-encoding other characters
// would leave the $defs key outside that set, so a name refuses them. A class
// local to a function is refused too: its only stable identity is its
// function's whole signature, and a lambda has none. Neither refusal touches a
// type that is not recursive, whose schema is written in place.
//
// A name does not mark where an argument list ends, long and long long have
// one width, a namespace can be named `anonymous`, and a class at global scope
// can be named `string`, so distinct types can still spell one name; the
// SupportedValue check rejects a schema that would define both.

// A definition name, or why there is none.
using name_result = std::expected<std::string, std::string>;

consteval name_result class_name(std::meta::info type);
consteval name_result argument_name(std::meta::info argument);

consteval bool is_name_character(const char character) {
  return (character >= 'a' && character <= 'z') ||
         (character >= 'A' && character <= 'Z') ||
         (character >= '0' && character <= '9') || character == '_';
}

// `first`, `separator`, and `second`, or the first of their failures.
consteval name_result joined(name_result first, const std::string_view separator,
                             const name_result& second) {
  if (!first) {
    return first;
  }
  if (!second) {
    return second;
  }
  first->append(separator);
  first->append(*second);
  return first;
}

consteval std::string with_dot(std::string name) {
  name.push_back('.');
  return name;
}

consteval name_result identifier_name(const std::meta::info entity) {
  if (!std::meta::has_identifier(entity)) {
    return std::unexpected(display(entity) + " has no name");
  }
  std::string name{};
  for (const char character : std::meta::identifier_of(entity)) {
    if (!is_name_character(character)) {
      return std::unexpected(display(entity) +
                             " has a name that is not ASCII letters, digits, and '_'");
    }
    name.push_back(character);
  }
  return name;
}

// The scopes around `entity`, outermost first, each followed by '.'.
consteval name_result scope_prefix(const std::meta::info entity) {
  const auto scope = std::meta::parent_of(entity);
  // Parenthesized so clang-format, which does not know `^^`, lays it out.
  if (scope == (^^::)) {
    return std::string{};
  }
  if (std::meta::is_function(scope)) {
    return std::unexpected(display(entity) +
                           " is local to a function; declare it in a namespace");
  }
  if (!std::meta::is_namespace(scope)) {
    return class_name(scope).transform(with_dot);
  }
  return joined(scope_prefix(scope), "",
                std::meta::has_identifier(scope) ? identifier_name(scope)
                                                 : name_result{"anonymous"})
      .transform(with_dot);
}

consteval name_result class_name(const std::meta::info type) {
  const bool specialization = std::meta::has_template_arguments(type);
  auto name =
      joined(scope_prefix(type), "",
             identifier_name(specialization ? std::meta::template_of(type) : type));
  if (specialization) {
    for (const auto argument : std::meta::template_arguments_of(type)) {
      name = joined(std::move(name), "-", argument_name(argument));
    }
  }
  return name;
}

// `word` followed by the first `count` of `arguments`.
consteval name_result spelled_template(std::string word,
                                       const std::vector<std::meta::info>& arguments,
                                       const std::size_t count) {
  name_result name = std::move(word);
  for (std::size_t index = 0; index < count; ++index) {
    name = joined(std::move(name), "-", argument_name(arguments[index]));
  }
  return name;
}

// The name of a standard container a definition name spells, if `type` is one.
consteval std::optional<name_result> container_name(const std::meta::info type) {
  if (!std::meta::has_template_arguments(type)) {
    return std::nullopt;
  }
  const auto arguments = std::meta::template_arguments_of(type);
  if (is_specialization_of(type, ^^std::optional)) {
    return spelled_template("optional", arguments, 1);
  }
  if (is_specialization_of(type, ^^std::array)) {
    return spelled_template("array", arguments, 2);
  }
  if (is_specialization_of(type, ^^std::variant)) {
    return spelled_template("variant", arguments, arguments.size());
  }
  const bool default_allocator =
      arguments.size() == 2 &&
      std::meta::dealias(arguments[1]) ==
          std::meta::substitute((^^std::allocator), {arguments[0]});
  if (default_allocator && is_specialization_of(type, ^^std::vector)) {
    return spelled_template("vector", arguments, 1);
  }
  if (default_allocator && is_specialization_of(type, ^^std::indirect)) {
    return spelled_template("indirect", arguments, 1);
  }
  return std::nullopt;
}

// The name of a type that needs no scope, or empty.
consteval std::string leaf_name(const std::meta::info type) {
  for (const auto& [leaf, name] :
       {std::pair{(^^bool), "bool"}, std::pair{(^^char), "char"},
        std::pair{(^^wchar_t), "wchar_t"}, std::pair{(^^char8_t), "char8_t"},
        std::pair{(^^char16_t), "char16_t"}, std::pair{(^^char32_t), "char32_t"},
        std::pair{(^^float), "float"}, std::pair{(^^double), "double"},
        std::pair{plain_type(^^std::string), "string"}}) {
    if (type == leaf) {
      return name;
    }
  }
  if (std::meta::is_integral_type(type)) {
    return (std::meta::is_signed_type(type) ? "int" : "uint") +
           decimal(std::meta::size_of(type) * 8U);
  }
  return {};
}

consteval name_result type_name(const std::meta::info reflected) {
  const auto type = std::meta::dealias(reflected);
  const bool qualified = std::meta::is_reference_type(type) ||
                         std::meta::is_const_type(type) ||
                         std::meta::is_volatile_type(type);
  if (!qualified) {
    if (auto leaf = leaf_name(type); !leaf.empty()) {
      return leaf;
    }
    if (auto container = container_name(type)) {
      return *std::move(container);
    }
    if (std::meta::is_enum_type(type) ||
        (std::meta::is_class_type(type) && is_complete_class(type) &&
         std::meta::is_aggregate_type(type))) {
      return class_name(type);
    }
  }
  return std::unexpected(
      display(type) +
      " is not a type that a definition name spells; an argument type must be "
      "bool, a character or integer type, float, double, std::string, a "
      "std::optional, std::vector, std::indirect, std::array, or std::variant of "
      "such types, an aggregate, or an enum");
}

// An integral constant, widened so that consteval code can read it without
// naming its type, as annotation_text_v reads an annotation.
template <auto Value>
inline constexpr std::int64_t signed_constant_v = static_cast<std::int64_t>(Value);

template <auto Value>
inline constexpr std::uint64_t unsigned_constant_v = static_cast<std::uint64_t>(Value);

consteval std::string signed_decimal(const std::int64_t value) {
  // The magnitude of the lowest int64 fits a uint64 but not an int64.
  return value < 0 ? "minus" + decimal(static_cast<std::uint64_t>(-(value + 1)) + 1U)
                   : decimal(static_cast<std::uint64_t>(value));
}

consteval name_result enumerator_name(const std::meta::info constant) {
  for (const auto enumerator :
       std::meta::enumerators_of(std::meta::type_of(constant))) {
    if (std::meta::constant_of(enumerator) == constant) {
      return identifier_name(enumerator);
    }
  }
  return std::unexpected(display(constant) + " is not a declared enumerator");
}

consteval name_result constant_name(const std::meta::info constant) {
  const auto type = plain_type(std::meta::type_of(constant));
  if (type == (^^bool)) {
    return std::meta::extract<bool>(constant) ? "true" : "false";
  }
  if (std::meta::is_enum_type(type)) {
    return enumerator_name(constant);
  }
  if (std::meta::is_integral_type(type) &&
      std::meta::size_of(type) <= sizeof(std::uint64_t)) {
    // Parenthesized so clang-format, which does not know `^^`, lays them out.
    return std::meta::is_signed_type(type)
               ? signed_decimal(std::meta::extract<std::int64_t>(
                     std::meta::substitute((^^signed_constant_v), {constant})))
               : decimal(std::meta::extract<std::uint64_t>(
                     std::meta::substitute((^^unsigned_constant_v), {constant})));
  }
  return std::unexpected(display(constant) +
                         " is not a constant that a definition name spells; an "
                         "argument constant must be an integer, a bool, or an "
                         "enumerator");
}

consteval name_result argument_name(const std::meta::info argument) {
  if (std::meta::is_type(argument)) {
    return type_name(argument);
  }
  if (std::meta::is_value(argument)) {
    return constant_name(argument);
  }
  return std::unexpected(display(argument) + " is neither a type nor a constant");
}

consteval name_result definition_name(const schema_node node) {
  auto name = class_name(node.type);
  if (name && !node.tagged && tag_of(node.type).has_value()) {
    name->append("-untagged");
  }
  return name;
}

// The nodes reachable from `start` that can contain themselves, other than
// `root`: a schema is its root, and refers to it as "#".
consteval std::vector<schema_node>
recursive_nodes(std::vector<schema_node> start, const std::optional<schema_node> root) {
  std::vector<schema_node> recursive{};
  for (const auto node : reachable_from(std::move(start))) {
    if (node != root && is_recursive(node)) {
      recursive.push_back(node);
    }
  }
  return recursive;
}

// The nodes that a schema of `type` writes once, under $defs, and refers to
// everywhere else, in the order of their names: each reachable form of an
// aggregate that can contain itself, other than a recursive root. Only a type
// the SupportedValue check admits gets here, so every name exists and differs.
consteval std::vector<schema_node> definitions_of(const std::meta::info type) {
  std::vector<schema_node> start{};
  append_held_nodes(start, type);
  auto definitions = recursive_nodes(std::move(start), root_node(type));
  std::ranges::sort(definitions, {}, [](const schema_node node) {
    return definition_name(node).value();
  });
  return definitions;
}

} // namespace scrydantic::detail
