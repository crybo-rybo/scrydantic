#pragma once

#include <algorithm>
#include <cstddef>
#include <expected>
#include <meta>
#include <scrydantic/detail/constraints.hpp>
#include <scrydantic/detail/decode.hpp>
#include <scrydantic/detail/failure.hpp>
#include <scrydantic/detail/model.hpp>
#include <scrydantic/error.hpp>
#include <scrydantic/json.hpp>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

// Checking a value the program built against the constraints and validators of
// its type. The checks are the decoder's own (accepts_member and passes_checks,
// in decode.hpp), given a no_json where they take the JSON value that a decoded
// value came from, and they report through the decoder's cursor into the
// decoder's report. So an issue has the kind, the path, and the reason that
// decoding the value's JSON would give it, max_issues stops this walk where it
// stops that one, and there is one statement of what a constraint means.
//
// Only the walk is new: it visits a value where the decoder visits a document.
// It goes in the decoder's order, which is the order the issues are reported
// in: an object's members in declaration order, a flattened member's where it
// stands, an array's elements by index, and a map's entries by key. A value is
// checked from the inside out, as it decodes: what a member holds, then the
// member's constraints, then its validators, and the class's validators last,
// each only if all before it found nothing. Every loop checks whether the
// report is full before it descends, as the decoder's do.
//
// The walk is not generated for a type that has no constraint and no validator
// anywhere in it, nor for a part of a value whose type has none (has_checks_v).
//
// A value can hold what no document does, and all of it is what encode()
// refuses, so it is left to encode() and passed over here: a disengaged
// optional has nothing to check, and neither has an indirect that a move left
// without a value or a variant that an exception did. Nor has an array or an
// object nested deeper than max_json_depth, which bounds this recursion as it
// bounds the encoder's: a value can nest without bound, and no decoded one does.
namespace scrydantic::detail {

// `depth` counts the arrays and objects open around `value`, as the encoder
// counts them.
template <typename Type>
[[nodiscard]] bool validate_value(const Type& value, const decode_cursor& here,
                                  std::size_t depth);

// Checks one member of `object`, whose own class declares it, and returns false
// if there was an issue in it or about it. Root is the type of the JSON object
// the member is a field of, which names its key: the class of `object`, or one
// that flattens it. `depth` counts the arrays and objects open around the
// member's value.
template <typename Root, std::meta::info Member, typename Owner>
[[nodiscard]] bool validate_member(const Owner& object, const decode_cursor& parent,
                                   const std::size_t depth) {
  constexpr field_layout field = fields_v<Root>[field_position_v<Root, Member>];
  constexpr std::string_view key = field.key_view();
  const decode_cursor here = parent.member(key);
  const auto& value = object.[:Member:];
  if (!validate_value(value, here, depth)) {
    return false;
  }
  if constexpr (!checked_constraints_v<Member>.empty() ||
                !validators_v<Member>.empty()) {
    return accepts_member<Member>(value, no_json{}, here);
  } else {
    return true;
  }
}

// Checks the members of `object`, which is the Root object or a member
// flattened into it at any depth, and then runs its class's validators, if
// every member was valid. A flattened member's members are fields of the Root
// object, so they are checked at its path. `depth` counts the arrays and
// objects open around each member's value, the object among them.
template <typename Root, typename Owner>
[[nodiscard]] bool validate_object(const Owner& object, const decode_cursor& here,
                                   const std::size_t depth) {
  bool valid = true;
  template for (constexpr std::meta::info member : declared_members_v<Owner>) {
    if (here.stopped()) {
      return false;
    }
    if constexpr (is_flattened(member)) {
      if (!validate_object<Root>(object.[:member:], here, depth)) {
        valid = false;
      }
    } else if (!validate_member<Root, member>(object, here, depth)) {
      valid = false;
    }
  }
  return valid && passes_checks<^^Owner>(object, here);
}

// A vector or a fixed array. `depth` counts the arrays and objects open around
// each element, the sequence among them.
template <typename Sequence>
[[nodiscard]] bool validate_sequence(const Sequence& values, const decode_cursor& here,
                                     const std::size_t depth) {
  bool valid = true;
  for (std::size_t index = 0; index < values.size() && !here.stopped(); ++index) {
    if (!validate_value(values[index], here.element(index), depth)) {
      valid = false;
    }
  }
  return valid;
}

// The entries are checked in canonical key order, the order a document holds
// them in, whatever order the map iterates in: it is the order of the issues,
// and it decides which of them a full report keeps. A map that does not iterate
// in that order has pointers to its entries sorted first, as write_map() sorts
// them. That is repeated here rather than taken out of the encoder for the two
// to share, which would change the code an encode of a map compiles to. A key
// is the map's own string, which outlives the walk, as decode_cursor::entry()
// requires. `depth` counts the arrays and objects open around each entry's
// value.
template <typename Map>
[[nodiscard]] bool validate_map(const Map& values, const decode_cursor& here,
                                const std::size_t depth) {
  using Entry = typename Map::value_type;
  bool valid = true;
  const auto validate_entry = [&here, &valid, depth](const Entry& entry) {
    if (!validate_value(entry.second, here.entry(entry.first), depth)) {
      valid = false;
    }
  };
  if constexpr (iterates_in_key_order(^^Map)) {
    for (const Entry& entry : values) {
      if (here.stopped()) {
        break;
      }
      validate_entry(entry);
    }
  } else {
    std::vector<const Entry*> entries{};
    entries.reserve(values.size());
    for (const Entry& entry : values) {
      entries.push_back(&entry);
    }
    std::ranges::sort(entries, {}, [](const Entry* const entry) {
      return std::string_view{entry->first};
    });
    for (const Entry* const entry : entries) {
      if (here.stopped()) {
        break;
      }
      validate_entry(*entry);
    }
  }
  return valid;
}

template <typename Type>
bool validate_value([[maybe_unused]] const Type& value,
                    [[maybe_unused]] const decode_cursor& here,
                    [[maybe_unused]] const std::size_t depth) {
  constexpr auto kind = kind_of(^^Type);
  if constexpr (!has_checks_v<Type>) {
    return true;
  } else if constexpr (kind == value_kind::optional) {
    return !value.has_value() || validate_value(*value, here, depth);
  } else if constexpr (kind == value_kind::indirect) {
    return value.valueless_after_move() || validate_value(*value, here, depth);
  } else if (depth == max_json_depth) {
    // Every other type that has a check is an array or an object in JSON.
    return true;
  } else if constexpr (kind == value_kind::vector || kind == value_kind::array) {
    return validate_sequence(value, here, depth + 1);
  } else if constexpr (kind == value_kind::map) {
    return validate_map(value, here, depth + 1);
  } else if constexpr (kind == value_kind::variant) {
    // A variant is the object its alternative is, which is where the object is
    // counted. Its tag is no member of the alternative, so nothing checks it.
    return value.valueless_by_exception() ||
           std::visit(
               [&here, depth](const auto& alternative) {
                 return validate_value(alternative, here, depth);
               },
               value);
  } else {
    return validate_object<Type>(value, here, depth + 1);
  }
}

// Checks a value whose type has been checked against a family: the public
// validate()'s work. The failure is the one a decode builds from the same
// issues.
template <typename Type> [[nodiscard]] Status validate(const Type& value) {
  if constexpr (has_checks_v<Type>) {
    decode_report report{};
    if (!validate_value(value, decode_cursor{report}, 0)) {
      return std::unexpected(decode_error(std::move(report)));
    }
  }
  return {};
}

} // namespace scrydantic::detail
