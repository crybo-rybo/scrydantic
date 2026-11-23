#pragma once

#include <algorithm>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <meta>
#include <scrydantic/detail/constraints.hpp>
#include <scrydantic/detail/json_number.hpp>
#include <scrydantic/detail/json_string.hpp>
#include <scrydantic/detail/json_text.hpp>
#include <scrydantic/detail/model.hpp>
#include <scrydantic/json.hpp>
#include <span>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

// The value a member has when its key is absent, as the JSON a schema states
// under "default". It is the member of a default-constructed object of its
// class, since that is what decoding leaves an absent member with, and it is
// spelled as encode() spells that value: the same keys in the same order, the
// same escapes, the same digits.
//
// A schema is a constant, so the object is built and spelled in a constant
// expression, by a writer of its own. encode()'s writer is not one: it spells a
// number with std::to_chars, which is not constexpr for floating-point types,
// and validates strings and payloads in the compiled JSON core. So this writer
// follows encode()'s, kind for kind, and gives up wherever it cannot be sure of
// writing the same text. A default it gives up on has no "default" keyword; the
// schema is otherwise the same. tests/codec/schema_default_tests.cpp holds the
// two writers to the same text.
namespace scrydantic::detail {

template <typename Type>
consteval bool append_default(std::string& output, const Type& value,
                              std::size_t depth);

template <typename Integer>
consteval void append_default_integer(std::string& output, const Integer value) {
  if constexpr (std::signed_integral<Integer>) {
    if (value < 0) {
      output.push_back('-');
      // Through the unsigned type, so that the lowest value's magnitude does not
      // overflow.
      output += decimal(std::uint64_t{0} - static_cast<std::uint64_t>(value));
      return;
    }
  }
  output += decimal(static_cast<std::uint64_t>(value));
}

// The number of significant digits in the shortest decimal that reads back as
// `magnitude`, a positive finite double.
consteval std::size_t significant_digits(const double magnitude) {
  const auto digits = shortest_decimal(magnitude).digits;
  return digits.find_last_not_of('0') + 1U;
}

// Whether append_canonical_double() spells `number`, the value of a member of
// type Float, as encode() spells that member.
//
// encode() spells a double below 2^53 as append_canonical_double() does. From
// there up it writes std::to_chars' text wherever that text is digits alone that
// fit a 64-bit integer, which std::to_chars decides by length, so only from 2^64
// up, where no text fits, is the spelling the canonical one again.
//
// encode() spells a float as the double nearest the float's shortest decimal,
// which is not the float's own value: 0.1f is written `0.1`, and the double it
// is as `0.10000000149011612`. The two are one number exactly when the double's
// shortest decimal is also the float's, and it is whenever it has at most six
// significant digits: every decimal that short reads back as a float of its own
// (std::numeric_limits<float>::digits10), so no other one that short, and none
// shorter, reads back as this float. Spelling any other float would take
// finding its shortest decimal, which nothing here does at compile time.
template <std::floating_point Float>
consteval bool spells_as_encoded(const double number) {
  const double magnitude = number < 0.0 ? -number : number;
  if (magnitude == 0.0) {
    return true;
  }
  if (std::same_as<Float, float>) {
    return magnitude >= static_cast<double>(std::numeric_limits<float>::min()) &&
           significant_digits(magnitude) <=
               static_cast<std::size_t>(std::numeric_limits<float>::digits10);
  }
  return magnitude < 0x1p53 || magnitude >= 0x1p64;
}

template <std::floating_point Float>
consteval bool append_default_number(std::string& output, const Float value) {
  const double number = value;
  if (!is_finite(number) || !spells_as_encoded<Float>(number)) {
    return false;
  }
  append_canonical_double(output, number);
  return true;
}

template <typename Enum>
consteval bool append_default_enumerator(std::string& output, const Enum value) {
  template for (constexpr std::meta::info enumerator : enumerators_v<Enum>) {
    if (value == [:enumerator:]) {
      const auto name = json_name_of(enumerator);
      append_json_string(output, name);
      return is_utf8(name);
    }
  }
  return false;
}

template <typename Sequence>
consteval bool append_default_sequence(std::string& output, const Sequence& value,
                                       const std::size_t depth) {
  if (depth == max_json_depth) {
    return false;
  }
  output.push_back('[');
  for (std::size_t index = 0; index < value.size(); ++index) {
    if (index != 0) {
      output.push_back(',');
    }
    if (!append_default(output, value[index], depth + 1)) {
      return false;
    }
  }
  output.push_back(']');
  return true;
}

// As write_object() writes it: the members in the order of their keys, a
// disengaged member that skip_null covers left out, and the tag of a variant
// alternative at its place among them.
template <typename Type, bool Tagged>
consteval bool append_default_object(std::string& output, const Type& value,
                                     const std::size_t depth) {
  if (depth == max_json_depth) {
    return false;
  }
  output.push_back('{');
  bool first = true;
  const auto separate = [&output, &first] {
    if (!first) {
      output.push_back(',');
    }
    first = false;
  };
  bool tag_pending = Tagged;
  const auto write_tag = [&] {
    if (tag_pending) {
      separate();
      append_json_string(output, tag_key);
      output.push_back(':');
      append_json_string(output, tag_of(^^Type).value_or(""));
      tag_pending = false;
    }
  };
  if (Tagged && !is_utf8(tag_of(^^Type).value_or(""))) {
    return false;
  }
  template for (constexpr field_layout field : fields_v<Type>) {
    constexpr std::meta::info member = field.member;
    constexpr std::string_view key = field.key_view();
    if (!is_utf8(key)) {
      return false;
    }
    if (key > tag_key) {
      write_tag();
    }
    const auto& owner = declaring_object<field.path, field.depth>(value);
    bool present = true;
    if constexpr (field.omit_null) {
      present = owner.[:member:].has_value();
    }
    if (present) {
      separate();
      append_json_string(output, key);
      output.push_back(':');
      if (!append_default(output, owner.[:member:], depth + 1)) {
        return false;
      }
    }
  }
  write_tag();
  output.push_back('}');
  return true;
}

// The alternative a variant holds, as write_variant() writes it: a tagged one
// as its object with the tag, a std::monostate as null, and any other as its
// type's value.
template <typename Variant>
consteval bool append_default_variant(std::string& output, const Variant& value,
                                      const std::size_t depth) {
  template for (constexpr std::meta::info alternative : alternatives_v<Variant>) {
    using Alternative = [:alternative:];
    if (const auto* const held = std::get_if<Alternative>(&value)) {
      if constexpr (is_tagged_alternative(alternative)) {
        return append_default_object<Alternative, true>(output, *held, depth);
      } else if constexpr (std::same_as<Alternative, std::monostate>) {
        append_json_literal(output, "null");
        return true;
      } else {
        return append_default(output, *held, depth);
      }
    }
  }
  // Left valueless by an exception.
  return false;
}

// A leaf: a value that holds no other.
template <typename Type>
consteval bool append_default_leaf(std::string& output, const Type& value) {
  constexpr auto kind = kind_of(^^Type);
  if constexpr (kind == value_kind::boolean) {
    append_json_literal(output, value ? "true" : "false");
    return true;
  } else if constexpr (kind == value_kind::integer) {
    append_default_integer(output, value);
    return true;
  } else if constexpr (kind == value_kind::floating) {
    return append_default_number(output, value);
  } else if constexpr (kind == value_kind::string) {
    append_json_string(output, value);
    return is_utf8(value);
  } else if constexpr (kind == value_kind::enumeration) {
    return append_default_enumerator(output, value);
  } else {
    // A Json payload, whose canonical text only the compiled JSON core writes.
    return false;
  }
}

// Appends `value` as encode() writes it, with `depth` arrays and objects open
// around it. False when encode() would refuse the value, or would spell it in a
// way this writer cannot be sure of; `output` is then left partly written.
template <typename Type>
consteval bool append_default(std::string& output, const Type& value,
                              const std::size_t depth) {
  constexpr auto kind = kind_of(^^Type);
  if constexpr (kind == value_kind::optional) {
    if (!value.has_value()) {
      append_json_literal(output, "null");
      return true;
    }
    return append_default(output, *value, depth);
  } else if constexpr (kind == value_kind::indirect) {
    return !value.valueless_after_move() && append_default(output, *value, depth);
  } else if constexpr (kind == value_kind::vector || kind == value_kind::array) {
    return append_default_sequence(output, value, depth);
  } else if constexpr (kind == value_kind::variant) {
    return append_default_variant(output, value, depth);
  } else if constexpr (kind == value_kind::aggregate) {
    return append_default_object<Type, false>(output, value, depth);
  } else {
    return append_default_leaf(output, value);
  }
}

// The defaults of Owner's data members as JSON, in declaration order: empty for
// a member without an initializer, and for a default that cannot be spelled.
// One object serves every member. It is value-initialized, as decoding builds
// the one it fills (read_in_place), and it is not const: GCC 16 rejects the
// construction of a const aggregate whose member initializer builds a
// std::string, as a write to a const object.
template <typename Owner> consteval std::vector<std::string> spelled_defaults() {
  Owner object = Owner();
  std::vector<std::string> defaults{};
  template for (constexpr std::meta::info member : declared_members_v<Owner>) {
    std::string text{};
    if constexpr (std::meta::has_default_member_initializer(member)) {
      if (!append_default(text, object.[:member:], 0)) {
        text.clear();
      }
    }
    defaults.push_back(std::move(text));
  }
  return defaults;
}

template <typename Owner> consteval bool spells_defaults() {
  static_cast<void>(spelled_defaults<Owner>());
  return true;
}

// Whether a default-constructed Owner can be built and read in a constant
// expression. A member initializer that calls a function that is not constexpr,
// or reads a variable that is not constant, makes a class that cannot; it is as
// good a model as any other, and only its schema says less.
template <typename Owner>
concept constant_defaults =
    requires { typename std::bool_constant<spells_defaults<Owner>()>; };

// spelled_defaults() as static text, or nothing for a class whose defaults are
// not constants.
template <typename Owner> consteval std::span<const static_text> static_defaults() {
  std::vector<static_text> defaults{};
  if constexpr (constant_defaults<Owner>) {
    for (const auto& text : spelled_defaults<Owner>()) {
      const auto spelled = static_string(text);
      defaults.push_back({.data = spelled.data(), .size = spelled.size()});
    }
  }
  return std::define_static_array(defaults);
}

template <typename Owner>
inline constexpr std::span<const static_text> defaults_v = static_defaults<Owner>();

// The default of Owner's data member at `Index`, as a value that extract()
// returns, which a span is not: a schema asks for a default through a
// reflection, as annotation_text() reads an annotation.
template <typename Owner, std::size_t Index>
inline constexpr static_text default_text_v =
    Index < defaults_v<Owner>.size() ? defaults_v<Owner>[Index] : static_text{};

// The JSON of the value that `member`, a data member, has in a
// default-constructed object of its class, as encode() writes it; empty when the
// member has no initializer, or its default is not a constant that this writer
// can spell.
consteval std::string_view default_of(const std::meta::info member) {
  const auto owner = std::meta::parent_of(member);
  const auto members = data_members_of(owner);
  const auto index =
      static_cast<std::size_t>(std::ranges::find(members, member) - members.begin());
  // Parenthesized so clang-format, which does not know `^^`, lays it out.
  const auto variable = (^^default_text_v);
  const auto text = std::meta::extract<static_text>(
      std::meta::substitute(variable, {owner, std::meta::reflect_constant(index)}));
  return {text.data, text.size};
}

} // namespace scrydantic::detail
