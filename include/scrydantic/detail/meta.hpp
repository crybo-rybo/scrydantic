#pragma once

#include <algorithm>
#include <array>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <map>
#include <meta>
#include <scrydantic/annotations.hpp>
#include <scrydantic/detail/constraints.hpp>
#include <scrydantic/detail/keywords.hpp>
#include <scrydantic/detail/model.hpp>
#include <scrydantic/error.hpp>
#include <scrydantic/json.hpp>
#include <string>
#include <string_view>
#include <type_traits>
#include <unordered_map>
#include <utility>
#include <vector>

namespace scrydantic::detail {

// The three value families. They share one checker. Encoding only reads a value,
// so `encodable` alone admits std::string_view and a value that cannot be stored.
// Only `supported` has a schema, so it alone needs a name for each definition.
enum class value_family : std::uint8_t {
  supported,
  encodable,
  decodable,
};

consteval std::string at_path(const std::string& path, const std::string& text) {
  return path.empty() ? text : path + ": " + text;
}

// Decoding default-constructs a value, move-assigns decoded parts into it, and
// returns it by move.
inline constexpr std::string_view unstorable_reason =
    "must be default-constructible, move-constructible, and move-assignable";

consteval bool is_storable(const std::meta::info type) {
  return std::meta::is_default_constructible_type(type) &&
         std::meta::is_move_constructible_type(type) &&
         std::meta::is_move_assignable_type(type);
}

consteval bool is_qualified(const std::meta::info type) {
  const auto dealiased = std::meta::dealias(type);
  return std::meta::is_reference_type(dealiased) ||
         std::meta::is_const_type(dealiased) || std::meta::is_volatile_type(dealiased);
}

// Why the schema whose definitions are `definitions` cannot give each its own
// name, as the object of "has", or empty when it can. Sorted by name, two
// definitions that share one are adjacent.
consteval std::string
definition_names_problem(const std::vector<schema_node>& definitions) {
  struct named_definition {
    std::string name{};
    std::meta::info type{};
  };
  std::vector<named_definition> named{};
  for (const auto node : definitions) {
    auto name = definition_name(node);
    if (!name) {
      return "the recursive type " + display(node.type) +
             ", which no schema definition name can spell: " + name.error();
    }
    named.push_back({.name = std::move(*name), .type = node.type});
  }
  std::ranges::sort(named, {}, &named_definition::name);
  for (std::size_t index = 1; index < named.size(); ++index) {
    if (named[index].name == named[index - 1].name) {
      return "the recursive types " + display(named[index - 1].type) + " and " +
             display(named[index].type) +
             ", which both map to the schema definition name \"" + named[index].name +
             "\"";
    }
  }
  return {};
}

// The name a diagnostic gives an annotation of Scrydantic's own, such as
// "scrydantic::range", or empty for anyone else's. The test is the namespace
// the annotation's type is declared in, not a list of today's annotations, so
// one added later is covered wherever this is asked. The name is the type's, so
// skip_null is "scrydantic::skip_null_t".
consteval std::string own_annotation_name(const std::meta::info annotation) {
  const auto type = plain_type(std::meta::type_of(annotation));
  if (!std::meta::has_parent(type) || std::meta::parent_of(type) != (^^::scrydantic)) {
    return {};
  }
  return "scrydantic::" +
         owned_text(std::meta::identifier_of(std::meta::has_template_arguments(type)
                                                 ? std::meta::template_of(type)
                                                 : type));
}

// Explains why a type is outside a value family. The empty string means it is
// inside. The text names the offending member or element by its C++ path, and it
// is what the static_assert at every reflected entry point prints, so a rejected
// type fails with the reason rather than with an unsatisfied constraint.
class value_checker {
public:
  explicit consteval value_checker(const value_family family) : family_{family} {}

  // The shape of `type` first, then whether its recursion ends, and last, for a
  // type with a schema, whether that schema can name each of its definitions.
  consteval std::string check_root(const std::meta::info type) {
    if (auto text = check(type, {}); !text.empty()) {
      return text;
    }
    if (auto text = check_recursion(); !text.empty()) {
      return text;
    }
    return family_ == value_family::supported ? check_definition_names(type)
                                              : std::string{};
  }

  // A function parameter that a synthesized argument object holds as a member,
  // annotated as the parameter is. That object does not exist until its
  // parameters have passed, so the parameter is checked in the member's place:
  // its annotations and its constraints as a member's, by the member's own
  // checks, and its type as a root. The text names no path; the caller names
  // the parameter.
  consteval std::string check_parameter(const std::meta::info parameter) {
    if (auto text = check_member_annotations(parameter, {}); !text.empty()) {
      return text;
    }
    if (auto text = check_root(std::meta::type_of(parameter)); !text.empty()) {
      return text;
    }
    auto reason = constraint_problem(parameter);
    if (reason.empty()) {
      reason = validators_problem(parameter, constrained_type(parameter));
    }
    return reason;
  }

private:
  struct reached_aggregate {
    std::meta::info type{};
    // Where the shape check first reached the aggregate.
    std::string path{};
  };

  value_family family_;
  // Every aggregate the shape check has reached, in order. Each is checked where
  // it is first reached and nowhere else, so a recursive aggregate ends its own
  // walk, and an aggregate that many members share is not walked again.
  std::vector<reached_aggregate> reached_{};
  // The construction walk's aggregates: those it is inside, and those it has
  // finished. It finishes each once, so it visits every link once.
  std::vector<std::meta::info> constructing_{};
  std::vector<std::meta::info> constructed_{};

  [[nodiscard]] consteval bool decodes() const {
    return family_ != value_family::encodable;
  }

  consteval std::string check(const std::meta::info reflected,
                              const std::string& path) {
    const auto type = plain_type(reflected);
    const auto kind = kind_of(type);
    if (kind == value_kind::unsupported) {
      return unsupported(reflected, path);
    }
    if (kind <= value_kind::enumeration) {
      return check_leaf(kind, reflected, path);
    }
    return check_composite(kind, type, path);
  }

  static consteval std::string problem(const std::string& path,
                                       const std::meta::info type,
                                       const std::string_view reason) {
    return at_path(path, display(type) + " " + owned_text(reason));
  }

  static consteval std::string member_path(const std::string& path,
                                           const std::meta::info owner,
                                           const std::meta::info member) {
    return (path.empty() ? display(owner) + "::" : path + ".") +
           owned_text(std::meta::identifier_of(member));
  }

  // A field is named from the object down, through the flattened members that
  // lead to it: "page.next_cursor".
  static consteval std::string field_name(const declared_field& field) {
    std::string name{};
    for (const auto flattened : field.path) {
      name += owned_text(std::meta::identifier_of(flattened));
      name.push_back('.');
    }
    return name + owned_text(std::meta::identifier_of(field.member));
  }

  static consteval std::string field_path(const std::string& path,
                                          const std::meta::info owner,
                                          const declared_field& field) {
    return (path.empty() ? display(owner) + "::" : path + ".") + field_name(field);
  }

  // The fields of `type` that have a key. Every member of `type` has passed its
  // check by the time this is asked, but a flattened class may not have: in a
  // recursive model it can still be mid-check, with an unnamed member that the
  // check has yet to reach and report.
  static consteval std::vector<declared_field>
  keyed_fields(const std::meta::info type) {
    auto fields = declared_fields_of(type);
    std::erase_if(fields, [](const declared_field& field) {
      return !std::meta::has_identifier(field.member);
    });
    return fields;
  }

  static consteval std::string element_path(const std::string& path,
                                            const std::meta::info container) {
    return (path.empty() ? display(container) : path) + "[]";
  }

  // The value under any key of a map, as "[]" is any element of an array.
  static consteval std::string entry_path(const std::string& path,
                                          const std::meta::info map) {
    return (path.empty() ? display(map) : path) + "{}";
  }

  // A nested alternative is named after the variant's path ("Doc::blocks[]<Text>");
  // a root one starts its own ("Text::body").
  static consteval std::string alternative_path(const std::string& path,
                                                const std::meta::info alternative) {
    return path.empty() ? std::string{} : path + "<" + display(alternative) + ">";
  }

  consteval std::string check_leaf(const value_kind kind, const std::meta::info shown,
                                   const std::string& path) const {
    switch (kind) {
    case value_kind::string_view:
      return family_ == value_family::encodable
                 ? std::string{}
                 : problem(path, shown,
                           "is encode-only borrowed text; decoding and schemas "
                           "take std::string");
    case value_kind::enumeration:
      return check_enum(plain_type(shown), path);
    default:
      return {};
    }
  }

  // `shown` is the reflection as the checker received it, so an alias the
  // reflection still carries is printed as spelled rather than dealiased. GCC's
  // type_of already dealiases member types, so a member's std::chrono::seconds is
  // printed as the duration specialization it names.
  static consteval std::string unsupported(const std::meta::info shown,
                                           const std::string& path) {
    const auto type = plain_type(shown);
    if (std::meta::is_integral_type(type)) {
      if (!is_character_type(type)) {
        return problem(path, shown, "is wider than 64 bits");
      }
      // std::int8_t and std::uint8_t are these two, so they cannot be told apart.
      return problem(path, shown,
                     type == (^^signed char) || type == (^^unsigned char)
                         ? "is a character type, as std::int8_t and std::uint8_t "
                           "are; use std::int16_t or std::uint16_t for a number"
                         : "is a character type; use std::string for text or a "
                           "fixed-width integer of at least 16 bits for a number");
    }
    if (std::meta::is_union_type(type)) {
      return problem(path, shown,
                     "is a union; use a tagged std::variant of aggregates");
    }
    if (is_map_template(type)) {
      return problem(path, shown,
                     "has a key type other than std::string; a JSON object's keys "
                     "are strings");
    }
    if (is_specialization_of(type, ^^std::multimap) ||
        is_specialization_of(type, ^^std::unordered_multimap)) {
      return problem(path, shown,
                     "can hold one key more than once, which a JSON object cannot; "
                     "use std::map or std::unordered_map");
    }
    if (std::meta::is_class_type(type) && !is_complete_class(type)) {
      return problem(path, shown, "is an incomplete type");
    }
    return problem(path, shown, "is not a supported reflected value");
  }

  static consteval std::string check_enum(const std::meta::info type,
                                          const std::string& path) {
    if (!std::meta::is_scoped_enum_type(type)) {
      return problem(path, type, "is an unscoped enum; reflected enums are enum class");
    }
    if (annotation_count(type, ^^rename_all) > 1) {
      return problem(path, type, "has more than one scrydantic::rename_all annotation");
    }
    const auto enumerators = std::meta::enumerators_of(type);
    if (enumerators.empty()) {
      return problem(path, type, "has no enumerators");
    }
    for (const auto enumerator : enumerators) {
      if (auto text = check_enumerator_annotations(enumerator, path); !text.empty()) {
        return text;
      }
    }
    if (auto text = check_enumerator_values(type, path); !text.empty()) {
      return text;
    }
    return check_enumerator_names(type, path);
  }

  // An enumerator takes scrydantic::name and no other annotation of
  // Scrydantic's, since nothing reads one there. The test is the namespace the
  // annotation's type is declared in, not a list of today's annotations, so one
  // added later is rejected here until an enumerator gives it a meaning.
  static consteval std::string
  check_enumerator_annotations(const std::meta::info enumerator,
                               const std::string& path) {
    if (annotation_count(enumerator, ^^name) > 1) {
      return problem(path, enumerator, "has more than one scrydantic::name annotation");
    }
    for (const auto annotation : std::meta::annotations_of(enumerator)) {
      const auto own = own_annotation_name(annotation);
      if (!own.empty() && !annotation_matches(annotation, ^^name)) {
        return problem(path, enumerator,
                       "carries " + own +
                           ", which does not apply to enumerators; an enumerator "
                           "takes only scrydantic::name");
      }
    }
    return {};
  }

  static consteval std::string check_enumerator_values(const std::meta::info type,
                                                       const std::string& path) {
    const auto enumerators = std::meta::enumerators_of(type);
    for (std::size_t left = 0; left < enumerators.size(); ++left) {
      for (std::size_t right = left + 1; right < enumerators.size(); ++right) {
        if (std::meta::constant_of(enumerators[left]) ==
            std::meta::constant_of(enumerators[right])) {
          return problem(path, type,
                         "has enumerators " +
                             owned_text(std::meta::identifier_of(enumerators[left])) +
                             " and " +
                             owned_text(std::meta::identifier_of(enumerators[right])) +
                             " with one value, so that value has no single name");
        }
      }
    }
    return {};
  }

  // Two enumerators with one JSON name would make that string decode to
  // whichever is declared first, as two members with one key would write it twice.
  static consteval std::string check_enumerator_names(const std::meta::info type,
                                                      const std::string& path) {
    const auto enumerators = std::meta::enumerators_of(type);
    // Each name is asked for once, since under a rename_all it is derived.
    std::vector<std::string_view> names{};
    for (const auto enumerator : enumerators) {
      names.push_back(json_name_of(enumerator));
    }
    for (std::size_t left = 0; left < enumerators.size(); ++left) {
      for (std::size_t right = left + 1; right < enumerators.size(); ++right) {
        if (names[left] == names[right]) {
          return problem(path, type,
                         "has enumerators " +
                             owned_text(std::meta::identifier_of(enumerators[left])) +
                             " and " +
                             owned_text(std::meta::identifier_of(enumerators[right])) +
                             " that both map to the JSON string \"" +
                             owned_text(names[left]) + "\"");
        }
      }
    }
    return {};
  }

  consteval std::string check_composite(const value_kind kind,
                                        const std::meta::info type,
                                        const std::string& path) {
    if (kind == value_kind::aggregate) {
      return check_aggregate(type, path);
    }
    if (decodes() && !is_storable(type)) {
      return problem(path, type, unstorable_reason);
    }
    if (kind == value_kind::variant) {
      return check_variant(type, path);
    }
    if (kind == value_kind::map) {
      const auto mapped = mapped_of(type);
      return is_qualified(mapped)
                 ? problem(path, type, "has a cv-qualified or reference mapped type")
                 : check(mapped, entry_path(path, type));
    }
    const auto element = element_of(type);
    if (is_qualified(element)) {
      return problem(path, type, "has a cv-qualified or reference element");
    }
    if (kind == value_kind::optional) {
      const auto reason = null_problem(element);
      return reason.empty() ? check(element, path) : problem(path, type, reason);
    }
    // An indirect is the value it owns, on the wire and in the schema, so it adds
    // nothing to the path.
    if (kind == value_kind::indirect) {
      return kind_of(element) == value_kind::optional
                 ? problem(path, type,
                           "nests std::optional inside std::indirect; "
                           "std::optional<std::indirect<T>> is the nullable form")
                 : check(element, path);
    }
    if (kind == value_kind::vector && plain_type(element) == (^^bool)) {
      return problem(path, type,
                     "is std::vector<bool>, whose elements are not addressable; use "
                     "std::vector<std::uint16_t>, or std::array<bool, N> for a "
                     "fixed size");
    }
    return check(element, element_path(path, type));
  }

  // Why an optional of `element` would give null two meanings, as what the
  // optional does; empty when null is the empty optional alone. A variant's
  // std::monostate is null too: the schema would allow null twice, and decoding
  // could only ever read it as the empty optional.
  static consteval std::string_view null_problem(const std::meta::info element) {
    if (kind_of(element) == value_kind::optional) {
      return "nests std::optional directly; one optional layer is the only "
             "nullable form";
    }
    const auto owned = owned_type(element);
    if (kind_of(owned) == value_kind::variant &&
        alternative_of_kind(owned, alternative_kind::null) != std::meta::info{}) {
      return "holds a variant with a std::monostate alternative, so null would be "
             "both the empty optional and that alternative; keep one of the two";
    }
    return {};
  }

  consteval std::string check_aggregate(const std::meta::info type,
                                        const std::string& path) {
    // Reached again, an aggregate is either still being checked or has passed.
    if (std::ranges::contains(reached_, type, &reached_aggregate::type)) {
      return {};
    }
    if (!std::meta::bases_of(type, std::meta::access_context::unchecked()).empty()) {
      return problem(path, type, "has a base class");
    }
    if (auto text = check_class_annotations(type, path); !text.empty()) {
      return text;
    }
    reached_.push_back({.type = type, .path = path});
    for (const auto member : data_members_of(type)) {
      if (auto text = check_member(type, member, path); !text.empty()) {
        return text;
      }
    }
    // After the members, so a const member is reported as such rather than as
    // the missing assignment it causes.
    if (decodes() && !is_storable(type)) {
      return problem(path, type, unstorable_reason);
    }
    return check_keys(type, path);
  }

  static consteval std::string check_class_annotations(const std::meta::info type,
                                                       const std::string& path) {
    if (annotation_count(type, ^^tag) > 1) {
      return problem(path, type, "has more than one scrydantic::tag annotation");
    }
    if (annotation_count(type, ^^rename_all) > 1) {
      return problem(path, type, "has more than one scrydantic::rename_all annotation");
    }
    if (has_annotation(type, ^^name) || has_annotation(type, ^^emit_null_t)) {
      return problem(path, type,
                     "carries scrydantic::name or emit_null, which apply to "
                     "data members");
    }
    if (has_annotation(type, ^^flatten_t)) {
      return problem(path, type,
                     "carries scrydantic::flatten, which applies to data members");
    }
    if (has_annotation(type, ^^tool)) {
      return problem(path, type,
                     "carries scrydantic::tool, which applies to functions");
    }
    for (const auto annotation : std::meta::annotations_of(type)) {
      if (is_constraint(annotation)) {
        return problem(path, type,
                       "carries " + owned_text(constraint_name(annotation)) +
                           ", which applies to data members");
      }
    }
    auto reason = extra_keywords_problem(type);
    if (reason.empty()) {
      reason = validators_problem(type, type);
    }
    return reason.empty() ? reason : problem(path, type, reason);
  }

  consteval std::string check_member(const std::meta::info owner,
                                     const std::meta::info member,
                                     const std::string& path) {
    if (!std::meta::has_identifier(member)) {
      return problem(path, owner, "has an unnamed member");
    }
    const auto here = member_path(path, owner, member);
    const auto type = std::meta::dealias(std::meta::type_of(member));
    if (!std::meta::is_public(member)) {
      return at_path(here, "is not a public member");
    }
    if (std::meta::is_bit_field(member)) {
      return at_path(here, "is a bit-field");
    }
    // Encoding only reads, so an encode-only aggregate may borrow a value through
    // a reference member, as it borrows text through std::string_view.
    if (decodes() && std::meta::is_reference_type(type)) {
      return at_path(here, "is a reference member, which decoding cannot assign");
    }
    if (std::meta::is_volatile_type(std::meta::remove_reference(type))) {
      return at_path(here, "is a volatile member");
    }
    if (decodes() && std::meta::is_const_type(type)) {
      return at_path(here, "is a const member, which decoding cannot assign");
    }
    if (auto text = check_member_annotations(member, here); !text.empty()) {
      return text;
    }
    return check_member_value(member, here);
  }

  // The member's type, then its constraints and validators, which take a value
  // of that type.
  consteval std::string check_member_value(const std::meta::info member,
                                           const std::string& here) {
    if (auto text = check(value_type_of(member), here); !text.empty()) {
      return text;
    }
    auto reason = constraint_problem(member);
    if (reason.empty()) {
      reason = validators_problem(member, constrained_type(member));
    }
    return reason.empty() ? reason : at_path(here, reason);
  }

  // A flattened member is no member of the JSON object; its class's fields are.
  // So its type is a class with fields to give, which leaves out a
  // std::optional or std::indirect of one, and nothing describes a key or a
  // value that it does not have. A constraint, skip_null, and emit_null never
  // apply to a member of aggregate type, and are rejected as such.
  static consteval std::string check_flattened(const std::meta::info member,
                                               const std::string& here) {
    const auto type = std::meta::type_of(member);
    if (kind_of(type) != value_kind::aggregate) {
      return at_path(here, "carries scrydantic::flatten, which applies to members "
                           "of aggregate type, not to " +
                               display(type));
    }
    if (has_annotation(member, ^^name) || has_annotation(member, ^^description) ||
        has_annotation(member, ^^check_t)) {
      return at_path(here, "carries scrydantic::flatten together with "
                           "scrydantic::name, description, or check; a flattened "
                           "member has no JSON key or value of its own for them to "
                           "describe");
    }
    return {};
  }

  static consteval std::string check_member_annotations(const std::meta::info member,
                                                        const std::string& here) {
    if (has_annotation(member, ^^flatten_t)) {
      if (auto text = check_flattened(member, here); !text.empty()) {
        return text;
      }
    }
    if (annotation_count(member, ^^description) > 1) {
      return at_path(here, "a reflected member may have at most one "
                           "scrydantic::description annotation");
    }
    if (annotation_count(member, ^^name) > 1) {
      return at_path(here, "has more than one scrydantic::name annotation");
    }
    const bool skip = has_annotation(member, ^^skip_null_t);
    const bool emit = has_annotation(member, ^^emit_null_t);
    if ((skip || emit) && kind_of(value_type_of(member)) != value_kind::optional) {
      return at_path(here, "is not a std::optional, so skip_null and emit_null do not "
                           "apply to it");
    }
    if (skip && emit) {
      return at_path(here, "carries both skip_null and emit_null");
    }
    if (has_annotation(member, ^^tag) || has_annotation(member, ^^ignore_unknown_t)) {
      return at_path(here, "carries scrydantic::tag or ignore_unknown, which "
                           "apply to class types");
    }
    if (has_annotation(member, ^^rename_all)) {
      return at_path(here, "carries scrydantic::rename_all, which applies to "
                           "classes and enums; scrydantic::name gives one "
                           "member its key");
    }
    if (has_annotation(member, ^^tool)) {
      return at_path(here, "carries scrydantic::tool, which applies to "
                           "functions");
    }
    const auto reason = extra_keywords_problem(member);
    return reason.empty() ? reason : at_path(here, reason);
  }

  // Runs after every member passed, so each has an identifier and a valid key.
  // The keys are those of the whole object, so a flattened member's fields may
  // collide with the class's own and with another flattened member's.
  static consteval std::string check_keys(const std::meta::info type,
                                          const std::string& path) {
    const auto fields = keyed_fields(type);
    // Each key is asked for once, since under a rename_all it is derived.
    std::vector<std::string_view> keys{};
    for (const auto& field : fields) {
      keys.push_back(json_key_of(field.member));
    }
    for (std::size_t left = 0; left < fields.size(); ++left) {
      for (std::size_t right = left + 1; right < fields.size(); ++right) {
        if (keys[left] == keys[right]) {
          return problem(path, type,
                         "has members " + field_name(fields[left]) + " and " +
                             field_name(fields[right]) +
                             " that both map to the JSON key \"" +
                             owned_text(keys[left]) + "\"");
        }
      }
    }
    return {};
  }

  // Each alternative alone, and then every two of them together: a variant is
  // admitted when no JSON value could be read as either of two alternatives.
  consteval std::string check_variant(const std::meta::info type,
                                      const std::string& path) {
    const auto alternatives = alternatives_of(type);
    for (const auto alternative : alternatives) {
      if (auto text = check_alternative(type, alternative, path); !text.empty()) {
        return text;
      }
    }
    for (std::size_t left = 0; left < alternatives.size(); ++left) {
      for (std::size_t right = left + 1; right < alternatives.size(); ++right) {
        if (const auto reason =
                shared_value_problem(alternatives[left], alternatives[right]);
            !reason.empty()) {
          return problem(path, type, reason);
        }
      }
    }
    return {};
  }

  // Why some JSON value could be read as either of two alternatives of one
  // variant, as the object of the variant; empty when none could. Alternatives
  // of two kinds share no value: an integer and a floating-point alternative
  // divide the numbers by the integer's range. Those of one kind share every
  // value of it, but for two tagged aggregates, which share the objects of a
  // tag that both carry.
  static consteval std::string shared_value_problem(const std::meta::info left,
                                                    const std::meta::info right) {
    const auto kind = alternative_kind_of(left);
    if (kind != alternative_kind_of(right)) {
      return {};
    }
    const auto both = display(left) + " and " + display(right);
    if (is_tagged_alternative(left) && is_tagged_alternative(right)) {
      return *tag_of(left) == *tag_of(right)
                 ? "has alternatives " + both + " that share the tag \"" +
                       owned_text(*tag_of(left)) + "\""
                 : std::string{};
    }
    // An aggregate beside another is a tagged union that is missing a tag.
    if (kind == alternative_kind::object && kind_of(left) == value_kind::aggregate &&
        kind_of(right) == value_kind::aggregate) {
      const bool untagged_left = !is_tagged_alternative(left);
      return "has the alternative " + display(untagged_left ? left : right) +
             " without a scrydantic::tag annotation beside " +
             display(untagged_left ? right : left) +
             ", another JSON object; alternatives that are objects are told apart "
             "by their tags";
    }
    return "has alternatives " + both + " that share the JSON kind " +
           owned_text(words_of(kind).name) +
           "; the kind of a JSON value selects its alternative, and only "
           "aggregates that each carry a scrydantic::tag can share one";
  }

  // Why a type whose kind_of() is `kind` has no JSON kind of its own to be
  // selected by, as what follows "which"; empty for a type that is no supported
  // value at all, which the shape check explains.
  static consteval std::string_view kindless_reason(const value_kind kind) {
    switch (kind) {
    case value_kind::json:
      return "holds a value of any JSON kind, so no kind selects it; a "
             "scrydantic::Json in the variant's place holds what the variant "
             "would";
    case value_kind::optional:
      return "is null or its value, so it has no one JSON kind; std::monostate "
             "is the alternative that is null";
    case value_kind::indirect:
      return "is a std::indirect, which a variant does not see through; an "
             "aggregate alternative can hold it as a member";
    case value_kind::variant:
      return "is a variant itself, so it has no one JSON kind; list its "
             "alternatives in this variant";
    default:
      return {};
    }
  }

  consteval std::string check_alternative(const std::meta::info variant,
                                          const std::meta::info alternative,
                                          const std::string& path) {
    const auto shown = "has the alternative " + display(alternative) + ", which ";
    if (is_qualified(alternative)) {
      return problem(path, variant,
                     shown + "is cv-qualified; an alternative is a plain value type");
    }
    const auto kind = alternative_kind_of(alternative);
    if (kind == alternative_kind::null) {
      return {};
    }
    if (const auto reason = kindless_reason(kind_of(alternative));
        kind == alternative_kind::none && !reason.empty()) {
      return problem(path, variant, shown + owned_text(reason));
    }
    const auto here = alternative_path(path, alternative);
    if (auto text = check(alternative, here); !text.empty()) {
      return text;
    }
    if (!is_tagged_alternative(alternative)) {
      return {};
    }
    for (const auto& field : keyed_fields(plain_type(alternative))) {
      if (json_key_of(field.member) == tag_key) {
        return at_path(field_path(here, plain_type(alternative), field),
                       "has the JSON key \"type\", which carries the variant's tag");
      }
    }
    return {};
  }

  // Default construction builds every member, the value a std::indirect owns,
  // every element of a non-empty std::array, and a variant's first alternative;
  // it leaves a std::optional disengaged and a std::vector or a map empty.
  // Decoding starts from a default-constructed value, so on a cycle of the
  // former links alone it never ends. Encoding builds nothing, and the variant
  // it is given may hold any alternative, but a value with such a cycle that
  // passes through no variant is infinite or, through a reference, cyclic.
  // Reflection cannot see what a default member initializer holds, so the walk
  // assumes each member starts as its type's default. It starts from each
  // aggregate the shape check reached, at the path it was reached by.
  consteval std::string check_recursion() {
    for (const auto& [type, path] : reached_) {
      if (auto text = check_construction(type, path); !text.empty()) {
        return text;
      }
    }
    return {};
  }

  consteval std::string check_construction(const std::meta::info type,
                                           const std::string& path) {
    if (std::ranges::contains(constructed_, type)) {
      return {};
    }
    if (std::ranges::contains(constructing_, type)) {
      return problem(path, type,
                     decodes() ? "contains itself through no std::optional, "
                                 "std::vector, or variant alternative after the "
                                 "first, so default-constructing it, as decoding "
                                 "does, never ends"
                               : "contains itself through no std::optional, "
                                 "std::vector, or variant, so every value of it is "
                                 "infinite or cyclic and cannot be JSON");
    }
    constructing_.push_back(type);
    for (const auto member : data_members_of(type)) {
      if (auto text =
              check_filled(std::meta::type_of(member), member_path(path, type, member));
          !text.empty()) {
        return text;
      }
    }
    constructing_.pop_back();
    constructed_.push_back(type);
    return {};
  }

  // Follows the links of `reflected` that every value of it fills.
  consteval std::string check_filled(const std::meta::info reflected,
                                     const std::string& path) {
    const auto type = plain_type(reflected);
    switch (kind_of(type)) {
    case value_kind::aggregate:
      return check_construction(type, path);
    case value_kind::indirect:
      return check_filled(element_of(type), path);
    case value_kind::array:
      return array_size_of(type) == 0
                 ? std::string{}
                 : check_filled(element_of(type), element_path(path, type));
    case value_kind::variant: {
      if (!decodes()) {
        return {};
      }
      const auto first = alternatives_of(type).front();
      return check_filled(first, alternative_path(path, first));
    }
    default:
      return {};
    }
  }

  // A schema writes each recursive aggregate once, under a name spelled from its
  // type, so each must have a name of its own.
  static consteval std::string check_definition_names(const std::meta::info type) {
    std::vector<schema_node> start{};
    append_held_nodes(start, type);
    const auto reason =
        definition_names_problem(recursive_nodes(std::move(start), root_node(type)));
    return reason.empty() ? std::string{} : problem({}, type, "has " + reason);
  }
};

consteval std::string value_problem(const std::meta::info type,
                                    const value_family family) {
  return value_checker{family}.check_root(type);
}

// Why a function parameter cannot be a member of a synthesized argument object;
// empty when it can. See value_checker::check_parameter.
consteval std::string parameter_value_problem(const std::meta::info parameter) {
  return value_checker{value_family::supported}.check_parameter(parameter);
}

consteval std::string tool_arguments_problem(const std::meta::info type) {
  if (is_qualified(type)) {
    return display(type) + " is cv-qualified or a reference; tool arguments are a "
                           "plain aggregate type";
  }
  // A map does decode from one JSON object, so it is told what it lacks instead.
  if (kind_of(type) == value_kind::map) {
    return display(type) + " is a map, whose keys are data; tool arguments are a "
                           "plain aggregate, whose members name the arguments";
  }
  if (kind_of(type) != value_kind::aggregate) {
    return display(type) + " is not a plain aggregate; tool arguments decode from one "
                           "JSON object";
  }
  return value_problem(type, value_family::supported);
}

// Formats a rejection for static_assert. An empty view means the type satisfies
// the concept.
consteval std::string_view unsatisfied(const std::string_view subject,
                                       const std::string_view concept_name,
                                       const std::string& reason) {
  if (reason.empty()) {
    return {};
  }
  return static_string(owned_text(subject) + " does not satisfy scrydantic::" +
                       owned_text(concept_name) + ": " + reason);
}

template <typename Type> consteval std::string_view supported_diagnostic() {
  return unsatisfied(display(^^Type), "SupportedValue",
                     value_problem(^^Type, value_family::supported));
}

template <typename Type> consteval std::string_view encodable_diagnostic() {
  return unsatisfied(display(^^Type), "Encodable",
                     value_problem(^^Type, value_family::encodable));
}

// Decoding produces a value, so the target is a cv-unqualified object type.
template <typename Type> consteval std::string_view decodable_diagnostic() {
  return unsatisfied(display(^^Type), "Decodable",
                     is_qualified(^^Type)
                         ? display(^^Type) + " is cv-qualified or a reference; decode "
                                             "into the unqualified type"
                         : value_problem(^^Type, value_family::decodable));
}

template <typename Type> consteval std::string_view tool_arguments_diagnostic() {
  return unsatisfied(display(^^Type), "ToolArguments", tool_arguments_problem(^^Type));
}

// The wire names of a reflected type, as keys_v and tags_v list them. Each
// checks the type as encode() does, so a type outside Encodable fails with the
// same diagnostic, and then reads the names the codec itself writes.
template <typename Type> consteval std::string_view keys_diagnostic() {
  if constexpr (kind_of(^^Type) != value_kind::aggregate) {
    return static_string(display(^^Type) +
                         " is not a plain aggregate, so it has no JSON object keys");
  } else {
    return encodable_diagnostic<Type>();
  }
}

template <typename Type> consteval auto object_keys() {
  constexpr std::string_view problem = keys_diagnostic<Type>();
  static_assert(problem.empty(), problem);
  if constexpr (!problem.empty()) {
    return std::array<std::string_view, 0>{};
  } else {
    constexpr auto fields = field_layouts_v<Type>;
    std::array<std::string_view, fields.size()> keys{};
    for (std::size_t index = 0; index < fields.size(); ++index) {
      keys[index] = fields[index].key_view();
    }
    return keys;
  }
}

// The data member behind each of those keys, as field_members_v lists them: the
// member that declares the field, which for a field of a flattened member is a
// member of the flattened class.
template <typename Type> consteval auto object_field_members() {
  constexpr std::string_view problem = keys_diagnostic<Type>();
  static_assert(problem.empty(), problem);
  if constexpr (!problem.empty()) {
    return std::array<std::meta::info, 0>{};
  } else {
    constexpr auto fields = field_layouts_v<Type>;
    std::array<std::meta::info, fields.size()> members{};
    // Written through an iterator: an empty std::array's operator[] is not
    // constexpr in libstdc++, which a function that returns a reflection has to
    // be, so indexing would not compile for a class without members.
    std::ranges::transform(fields, members.begin(), &field_layout::member);
    return members;
  }
}

// tags_v pairs a tag with every alternative, by index, so it is defined for a
// variant whose every alternative carries one.
template <typename Type> consteval std::string_view tags_diagnostic() {
  if constexpr (kind_of(^^Type) != value_kind::variant) {
    return static_string(display(^^Type) + " is not a std::variant, so it has no tags");
  } else {
    if (const auto problem = encodable_diagnostic<Type>(); !problem.empty()) {
      return problem;
    }
    for (const auto alternative : alternatives_of(^^Type)) {
      if (!is_tagged_alternative(alternative)) {
        return static_string(display(^^Type) + " has the alternative " +
                             display(alternative) +
                             ", which carries no tag, so it does not have a tag "
                             "for every alternative");
      }
    }
    return {};
  }
}

template <typename Type> consteval auto variant_tags() {
  constexpr std::string_view problem = tags_diagnostic<Type>();
  static_assert(problem.empty(), problem);
  if constexpr (!problem.empty()) {
    return std::array<std::string_view, 0>{};
  } else {
    constexpr auto alternatives = alternatives_v<Type>;
    std::array<std::string_view, alternatives.size()> tags{};
    for (std::size_t index = 0; index < alternatives.size(); ++index) {
      tags[index] = *tag_of(alternatives[index]);
    }
    return tags;
  }
}

// enumerators_v checks an enum as encode() does, which is where an unscoped
// enum and two enumerators of one name are refused.
template <typename Type> consteval std::string_view enumerators_diagnostic() {
  if constexpr (kind_of(^^Type) != value_kind::enumeration) {
    return static_string(display(^^Type) +
                         " is not an enum, so it has no enumerator names");
  } else {
    return encodable_diagnostic<Type>();
  }
}

template <typename Type> consteval auto enumerator_json_names() {
  constexpr std::string_view problem = enumerators_diagnostic<Type>();
  static_assert(problem.empty(), problem);
  if constexpr (!problem.empty()) {
    return std::array<std::string_view, 0>{};
  } else {
    constexpr auto enumerators = declared_enumerators_v<Type>;
    std::array<std::string_view, enumerators.size()> names{};
    for (std::size_t index = 0; index < enumerators.size(); ++index) {
      names[index] = json_name_of(enumerators[index]);
    }
    return names;
  }
}

template <typename Type> struct expected_traits {
  static constexpr bool recognized = false;
};

template <typename Value> struct expected_traits<std::expected<Value, Error>> {
  static constexpr bool recognized = true;
  using value_type = Value;
};

// A result that tells the model only that the call succeeded: `void`, or a
// Status that may instead carry the handler's error. Either is sent as `{}`.
consteval bool is_acknowledgment(const std::meta::info result) {
  const auto type = plain_type(result);
  return type == (^^void) || type == plain_type(^^Status);
}

// The value a tool's return type sends to the model, which is the type its
// output schema describes: the value type of a Result, or else the type itself.
consteval std::meta::info result_value_type(const std::meta::info result) {
  const auto type = plain_type(result);
  const bool is_result =
      is_specialization_of(type, ^^std::expected) &&
      plain_type(std::meta::template_arguments_of(type)[1]) == (^^Error);
  return is_result ? std::meta::template_arguments_of(type)[0] : result;
}

// Why a tool's return type cannot be sent to the model; empty when it can. The
// text follows "returns", so it completes a sentence about the handler.
consteval std::string result_type_problem(const std::meta::info result) {
  if (std::meta::is_reference_type(result)) {
    return "the reference " + display(result) + "; a reflected tool returns a value";
  }
  if (is_acknowledgment(result)) {
    return {};
  }
  const auto reason = value_problem(result_value_type(result), value_family::supported);
  if (reason.empty()) {
    return {};
  }
  return display(result) +
         ", which is not void, scrydantic::Status, a SupportedValue, or a Result of "
         "one: " +
         reason;
}

template <typename Result>
consteval std::string handler_result_problem(const std::string& handler) {
  const auto reason = result_type_problem(^^Result);
  return reason.empty() ? std::string{} : handler + " returns " + reason;
}

// A binding's call context: a handler or a tool function may take one, as its
// leading parameter, by const reference. `void` means the binding has none.
template <typename Context>
concept with_context = !std::is_void_v<Context>;

// Whether a handler accepts the context. A handler that does is invoked that way.
// A handler is invoked as const, because the ToolFunction that stores it is.
template <typename Callable, typename Context, typename Args>
concept takes_context_first =
    with_context<Context> && std::invocable<const Callable&, const Context&, Args>;

// Whether a handler can be invoked when it is not const, in either form. Asked
// of one that cannot be invoked as const, it tells a mutable lambda, or a functor
// whose call operator is not const, from a callable that takes other arguments.
// A concept, so that `const Context&` is never formed for a void context.
template <typename Callable, typename Context, typename Args>
concept invocable_when_not_const =
    std::invocable<Callable&, Args> ||
    (with_context<Context> && std::invocable<Callable&, const Context&, Args>);

template <typename Callable, typename Context, typename Args>
consteval std::string not_invocable_problem(const std::string& handler) {
  if constexpr (invocable_when_not_const<Callable, Context, Args>) {
    return handler +
           " is invocable only when it is not const; a handler is called as const, "
           "so its call operator must be const, and state it changes must live "
           "outside it, such as behind a pointer it captures";
  } else if constexpr (with_context<Context>) {
    return handler + " is not invocable as (" + display(^^Args) + ") or as (" +
           display(^^const Context&) + ", " + display(^^Args) +
           "); a context parameter must come first";
  } else {
    return handler + " is not invocable as (" + display(^^Args) + ")";
  }
}

template <typename Handler, typename Args, typename Context>
consteval std::string tool_handler_problem() {
  using Callable = std::decay_t<Handler>;
  const auto handler = display(std::meta::dealias(^^Callable));
  if constexpr (!std::constructible_from<Callable, Handler> ||
                !std::move_constructible<Callable>) {
    return handler + " cannot be stored: a handler must be move-constructible";
  } else if constexpr (takes_context_first<Callable, Context, Args>) {
    // A handler that accepts the context is invoked that way, so its result type
    // is the one that has to be encodable. Checking the plain form as well would
    // accept a callable whose two arities disagree about their return.
    return handler_result_problem<
        std::invoke_result_t<const Callable&, const Context&, Args>>(handler);
  } else if constexpr (std::invocable<const Callable&, Args>) {
    return handler_result_problem<std::invoke_result_t<const Callable&, Args>>(handler);
  } else {
    return not_invocable_problem<Callable, Context, Args>(handler);
  }
}

template <typename Handler, typename Args, typename Context>
consteval std::string_view tool_handler_diagnostic() {
  return unsatisfied(display(std::meta::dealias(^^std::decay_t<Handler>)),
                     "ToolHandlerFor<" + display(^^Args) + ">",
                     tool_handler_problem<Handler, Args, Context>());
}

} // namespace scrydantic::detail
