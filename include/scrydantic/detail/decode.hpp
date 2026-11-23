#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <compare>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <meta>
#include <optional>
#include <scrydantic/decode_options.hpp>
#include <scrydantic/detail/constraints.hpp>
#include <scrydantic/detail/failure.hpp>
#include <scrydantic/detail/json_string.hpp>
#include <scrydantic/detail/json_text.hpp>
#include <scrydantic/detail/meta.hpp>
#include <scrydantic/detail/model.hpp>
#include <scrydantic/error.hpp>
#include <scrydantic/json.hpp>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <variant>

// Strict reflected decoding. Every function here is reached only through an entry
// point that has already checked the type against a value family, so none of them
// repeats the check.
//
// The decoder walks the document through json_node, the JSON core's borrowed
// handle on a value, under the JsonView that its entry point was given: `view`
// names the node a function reads.
//
// A failure does not end the decode. Each function reports what it finds through
// its cursor and goes on to the siblings of a value that failed, so one decode
// reports every issue until max_issues stops it. Every loop checks for that stop
// before it descends, so a function is only entered while the decode goes on,
// and a loop that finds it stopped has already seen one of its own values fail.
// A value fails when it, or any value inside it, does, and a member also fails
// when its decoded value misses one of its constraints or fails a check, as an
// object does that fails its class's checks. Those run only once the value has
// decoded without an issue, so while the decode goes on.
namespace scrydantic::detail {

template <typename Type>
[[nodiscard]] decode_result<Type> read_value(const json_node view,
                                             const decode_cursor& here);

// ---- Strings that stand for another value ------------------------------------
//
// DecodeOptions lets a string stand for the value its text spells. A reader asks
// for that value only where it has found the wrong kind and would report it, so
// a value of the kind its type requires is read exactly as it is without the
// options. The text is read by JsonView::parse(), so it is a number, a literal,
// an array, or an object by the rules every other JSON text is held to, and the
// reader goes on with the parsed value under the string's own cursor, as if it
// had stood in the string's place. Neither function returns a string, so the
// reader of what one returns does not ask again.
//
// The decode's report keeps the parsed value (decode_cursor::keep()), and the
// reader of an array or an object goes on with a node of it in place of the
// string. A reader that owned the value, or handed the branch to a function
// that owned it and read again from there, would have a destructor to run or a
// second caller, and GCC 16 then leaves out of line the readers that it
// otherwise inlines into their one caller: a call for each value of every
// decode, for a branch that few take. The two readers of numbers read what a
// string spells by calling themselves, which costs nothing: each has a caller
// for every member of its type and is out of line as it is.

// The boolean or number that the whole text of `view` spells, when `view` is a
// string and the decode coerces scalars; otherwise no value. A JSON scalar holds
// no whitespace, so text with any around it starts or ends with some.
[[nodiscard]] inline json_node spelled_scalar(const json_node view,
                                              const decode_cursor& here) {
  constexpr auto is_space = [](const char byte) {
    return byte == ' ' || byte == '\t' || byte == '\n' || byte == '\r';
  };
  if (!here.options().coerce_scalars) {
    return {};
  }
  const auto text = view.string();
  if (!text || text->empty() || is_space(text->front()) || is_space(text->back())) {
    return {};
  }
  auto spelled = JsonView::parse(*text);
  if (!spelled) {
    return {};
  }
  switch (spelled->kind()) {
  case JsonKind::boolean:
  case JsonKind::signed_integer:
  case JsonKind::unsigned_integer:
  case JsonKind::number:
    return json_node{here.keep(std::move(*spelled))};
  default:
    return {};
  }
}

// The array or object, as `kind` says, that the text of `view` spells, when
// `view` is a string and the decode coerces structures; otherwise no value.
// Every reader of an array or an object asks by the JSON kind it requires, so
// the coercion follows the kind and not the C++ type. The text stands where its
// string does, inside the arrays and objects open around `here`, and is refused
// when it would nest past max_json_depth there: the limit bounds the decoder's
// recursion, which goes on into the text. It is measured on the parsed value,
// which is what the decoder walks.
[[nodiscard]] inline json_node spelled_structure(const json_node view,
                                                 const decode_cursor& here,
                                                 const JsonKind kind) {
  if (!here.options().coerce_structures) {
    return {};
  }
  const auto text = view.string();
  if (!text) {
    return {};
  }
  auto spelled = JsonView::parse(*text);
  if (!spelled || spelled->kind() != kind ||
      here.depth() + spelled->depth() > max_json_depth) {
    return {};
  }
  return json_node{here.keep(std::move(*spelled))};
}

template <typename Integer>
[[nodiscard]] decode_result<Integer> read_integer(const json_node view,
                                                  const decode_cursor& here) {
  const auto narrow = [&here](const auto value) -> decode_result<Integer> {
    if (!std::in_range<Integer>(value)) {
      return here.fail(IssueKind::out_of_range, "is outside the integer range");
    }
    return static_cast<Integer>(value);
  };
  if (const auto value = view.signed_integer()) {
    return narrow(*value);
  }
  if (const auto value = view.unsigned_integer()) {
    return narrow(*value);
  }
  if (const json_node spelled = spelled_scalar(view, here)) {
    return read_integer<Integer>(spelled, here);
  }
  return here.fail(IssueKind::wrong_type, "must be an integer");
}

template <typename Float>
[[nodiscard]] decode_result<Float> read_float(const json_node view,
                                              const decode_cursor& here) {
  // Every 64-bit integer is within the range of a float, let alone a double, so
  // an integer converts straight to the member's type: one rounding, to the
  // nearest value, and never to zero or to infinity. Going through a wider type
  // first would round twice where that type cannot hold the integer exactly.
  static_assert(std::numeric_limits<Float>::max_exponent > 64);
  double value = 0.0;
  switch (view.kind()) {
  case JsonKind::signed_integer:
    return static_cast<Float>(*view.signed_integer());
  case JsonKind::unsigned_integer:
    return static_cast<Float>(*view.unsigned_integer());
  case JsonKind::number:
    value = *view.number();
    break;
  default:
    if (const json_node spelled = spelled_scalar(view, here)) {
      return read_float<Float>(spelled, here);
    }
    return here.fail(IssueKind::wrong_type, "must be a number");
  }

  // The parser reads no number as a double that is not finite, so only a
  // narrower Float has a range to check. It holds every double that rounds to
  // one of its values: those of a magnitude below `past`, the midpoint of its
  // largest value and the value one step beyond that, which a double holds
  // exactly. The midpoint itself rounds away from the largest value, whose
  // significand is odd. So the shortest text of the largest float,
  // 3.4028235e38, decodes to it, though as a double it is a little more.
  if (!std::isfinite(value)) {
    return here.fail(IssueKind::out_of_range, "must be a finite in-range number");
  }
  if constexpr (!std::same_as<Float, double>) {
    constexpr Float largest = std::numeric_limits<Float>::max();
    constexpr auto maximum = static_cast<double>(largest);
    constexpr double past =
        maximum +
        ((maximum - static_cast<double>(std::nextafter(largest, Float{0}))) / 2.0);
    if (value <= -past || value >= past) {
      return here.fail(IssueKind::out_of_range, "must be a finite in-range number");
    }
  }
  const auto converted = static_cast<Float>(value);
  if (value != 0.0 && converted == Float{0}) {
    return here.fail(IssueKind::out_of_range, "must be a finite in-range number");
  }
  return converted;
}

// The enumerator names are already in the generated schema's `enum` array, so
// repeating them in the failure keeps a retrying model from having to guess.
template <typename Enum>
[[nodiscard]] decode_result<Enum> read_enum(const json_node view,
                                            const decode_cursor& here) {
  if (view.kind() != JsonKind::string) {
    return here.fail(IssueKind::wrong_type, "must be an enumerator name");
  }
  const auto text = *view.string();
  template for (constexpr std::meta::info enumerator : declared_enumerators_v<Enum>) {
    if (text == json_name_of(enumerator)) {
      return [:enumerator:];
    }
  }
  return here.fail(IssueKind::unknown_enumerator,
                   std::string{"is not a declared enumerator; must be one of: "} +
                       std::string{enumerator_names(^^Enum)});
}

// The code points of `text`, which the parser has checked is UTF-8: every byte
// but a continuation byte, 10xxxxxx, starts one.
[[nodiscard]] inline std::size_t code_points(const std::string_view text) noexcept {
  return static_cast<std::size_t>(std::ranges::count_if(text, [](const char byte) {
    return (static_cast<unsigned char>(byte) & 0xC0U) != 0x80U;
  }));
}

// How `integer` compares with `number`, a finite double, exactly. Converting the
// integer to a double instead could round it onto the number.
template <std::integral Integer>
[[nodiscard]] constexpr std::partial_ordering
compare_exactly(const Integer integer, const double number) noexcept {
  // Both limits are doubles exactly: every Integer is below the first and at or
  // above the second.
  constexpr double past = std::signed_integral<Integer> ? 0x1p63 : 0x1p64;
  constexpr double lowest = std::signed_integral<Integer> ? -0x1p63 : 0.0;
  if (number >= past) {
    return std::partial_ordering::less;
  }
  if (number < lowest) {
    return std::partial_ordering::greater;
  }
  // Within the range, so the conversion truncates without overflow, and the
  // fraction it drops is exact.
  const auto whole = static_cast<Integer>(number);
  if (integer != whole) {
    return integer <=> whole;
  }
  return 0.0 <=> number - static_cast<double>(whole);
}

// How the number in `view` compares with `bound`, as the parser read the number,
// which is what the schema bounds: an integer as itself, and anything else as the
// double that its text rounds to.
[[nodiscard]] inline std::partial_ordering compare_parsed(const json_node view,
                                                          const double bound) noexcept {
  if (const auto integer = view.signed_integer()) {
    return compare_exactly(*integer, bound);
  }
  if (const auto integer = view.unsigned_integer()) {
    return compare_exactly(*integer, bound);
  }
  return *view.number() <=> bound;
}

// Whether `order`, how a value compares with `end`, keeps the value within it.
[[nodiscard]] constexpr bool within(const std::partial_ordering order,
                                    const checked_end& end) noexcept {
  if (end.lower) {
    return end.exclusive ? order > 0 : order >= 0;
  }
  return end.exclusive ? order < 0 : order <= 0;
}

// What a check is given in place of the JSON value its value decoded from, when
// the value decoded from none: it is the program's own, which validate() checks.
// The checks below are decoding's and validate()'s alike, and take either. Only
// a floating-point number is checked differently for it, as meets_end explains,
// and that is decided where a check is instantiated, so decoding runs the
// instructions it ran when it was the only caller.
struct no_json {};

// Checks `value` against one end of a range, `limit` as a Number. A
// floating-point member is checked first as the parser read the number, so that
// decoding rejects whatever the schema does, and then as converted, so that the
// host's value is within the end too. Conversion rounds monotonically to values
// of Number, of which the limit is one, so it can carry a number onto an
// exclusive end but never past an end. A value that decoded from no JSON has no
// number that a parser read: the value is the only number there is, so it is
// checked as an integer is, once, against the end as it stands. A NaN is within
// no end.
template <typename Number, typename Source>
[[nodiscard]] bool meets_end(const Number value, const Number limit,
                             const checked_end& end, [[maybe_unused]] const Source view,
                             const decode_cursor& here) {
  if constexpr (std::floating_point<Number> && std::same_as<Source, json_node>) {
    if (!within(compare_parsed(view, static_cast<double>(limit)), end)) {
      here.report(IssueKind::violated_constraint, end.reason);
      return false;
    }
    if (end.exclusive && !within(value <=> limit, end)) {
      here.report(IssueKind::violated_constraint, end.rounded_reason);
      return false;
    }
  } else if (!within(value <=> limit, end)) {
    here.report(IssueKind::violated_constraint, end.reason);
    return false;
  }
  return true;
}

template <std::meta::info Constraint, typename Number, typename Source>
[[nodiscard]] bool meets_range(const Number value, const Source view,
                               const decode_cursor& here) {
  constexpr checked_range limits = checked_range_of(Constraint, ^^Number);
  if constexpr (limits.lower.present) {
    if (!meets_end(value, bound_value<Number>(limits.lower.value), limits.lower, view,
                   here)) {
      return false;
    }
  }
  if constexpr (limits.upper.present) {
    return meets_end(value, bound_value<Number>(limits.upper.value), limits.upper, view,
                     here);
  }
  return true;
}

// Whether an integer of `magnitude` is a multiple of a factor of odd * 2^exponent,
// exactly. The quotient is magnitude * 2^-exponent / odd, and odd shares no
// factor with a power of two: with exponent at most zero, any multiple of odd
// is one, and otherwise a multiple of 2^exponent whose quotient by it is.
[[nodiscard]] constexpr bool is_multiple(std::uint64_t magnitude,
                                         const checked_multiple& multiple) noexcept {
  if (multiple.two_exponent > 0) {
    if (multiple.two_exponent >= 64) {
      return magnitude == 0U;
    }
    const std::uint64_t power = std::uint64_t{1} << multiple.two_exponent;
    if (magnitude % power != 0U) {
      return false;
    }
    magnitude /= power;
  }
  return magnitude % multiple.odd == 0U;
}

// Whether the number in `view`, as the parser read it, is a multiple of
// `factor`. For a double, fmod is exact.
[[nodiscard]] inline bool is_parsed_multiple(const json_node view,
                                             const checked_multiple& multiple,
                                             const double factor) noexcept {
  if (const auto integer = view.signed_integer()) {
    // A signed integer here is negative: the parser reads others as unsigned.
    return is_multiple(std::uint64_t{0} - static_cast<std::uint64_t>(*integer),
                       multiple);
  }
  if (const auto integer = view.unsigned_integer()) {
    return is_multiple(*integer, multiple);
  }
  return std::fmod(*view.number(), factor) == 0.0;
}

// The factor is a value of Number, and for a floating-point Number a binary
// fraction, so fmod, which is exact, finds exactly the multiples that the
// schema's multipleOf means. A floating-point member is checked as the parser
// read the number and then as converted, as meets_end explains, and one that
// decoded from no JSON once, as it is. Neither a NaN nor an infinity is a
// multiple: fmod of either is a NaN.
template <std::meta::info Constraint, typename Number, typename Source>
[[nodiscard]] bool meets_multiple(const Number value,
                                  [[maybe_unused]] const Source view,
                                  const decode_cursor& here) {
  constexpr checked_multiple multiple = checked_multiple_of(Constraint, ^^Number);
  constexpr auto factor = bound_value<Number>(multiple.value);
  if constexpr (std::floating_point<Number> && std::same_as<Source, json_node>) {
    if (!is_parsed_multiple(view, multiple, static_cast<double>(factor))) {
      here.report(IssueKind::violated_constraint, multiple.reason);
      return false;
    }
    if (std::fmod(value, factor) != Number{0}) {
      here.report(IssueKind::violated_constraint, multiple.rounded_reason);
      return false;
    }
  } else if constexpr (std::floating_point<Number>) {
    if (std::fmod(value, factor) != Number{0}) {
      here.report(IssueKind::violated_constraint, multiple.reason);
      return false;
    }
  } else if (value % factor != 0) {
    here.report(IssueKind::violated_constraint, multiple.reason);
    return false;
  }
  return true;
}

// A size is checked the same whatever it was decoded from, and nothing here
// reads Source. It is a parameter so that decoding's check and validate()'s are
// two specializations: where one specialization keyed on an annotation is
// reached from two instantiations, GCC 16 emits it twice under one symbol,
// which the assembler refuses in a build that inlines neither.
template <std::meta::info Constraint, typename Source>
[[nodiscard]] bool meets_size(const std::size_t size, const decode_cursor& here) {
  constexpr checked_size limits = checked_size_of(Constraint);
  if constexpr (limits.min != 0U) {
    if (size < limits.min) {
      here.report(IssueKind::violated_constraint, limits.min_reason);
      return false;
    }
  }
  if constexpr (limits.max != std::numeric_limits<std::size_t>::max()) {
    if (size > limits.max) {
      here.report(IssueKind::violated_constraint, limits.max_reason);
      return false;
    }
  }
  return true;
}

// `view` is the JSON value that `value` decoded from, or a no_json.
template <std::meta::info Constraint, typename Value, typename Source>
[[nodiscard]] bool meets(const Value& value, const Source view,
                         const decode_cursor& here) {
  if constexpr (annotation_matches(Constraint, ^^range)) {
    return meets_range<Constraint>(value, view, here);
  } else if constexpr (annotation_matches(Constraint, ^^multiple_of)) {
    return meets_multiple<Constraint>(value, view, here);
  } else if constexpr (annotation_matches(Constraint, ^^length)) {
    return meets_size<Constraint, Source>(code_points(value), here);
  } else {
    return meets_size<Constraint, Source>(value.size(), here);
  }
}

// Runs the validators of `Entity`, a member or a class, on `value` in
// declaration order, and reports the first rejection, after which none runs: a
// validator may rely on the ones before it.
template <std::meta::info Entity, typename Value>
[[nodiscard]] bool passes_checks(const Value& value, const decode_cursor& here) {
  template for (constexpr std::meta::info validator : validators_v<Entity>) {
    if (const Status status = [:validator:](value); !status) {
      here.reject(status.error());
      return false;
    }
  }
  return true;
}

// Reports each constraint of `Member` that `value`, the member's value decoded
// from `view`, misses, in declaration order, and runs its validators on a value
// that misses none. Returns false if there was an issue. A range has one end a
// value can miss, since the checks admit no empty range. It sees through an
// optional and an indirect as constrained_type() does, so the validators take the
// value they own; a decoded indirect always owns one. The program's own
// indirect, which `view` is a no_json for, owns none once it has been moved
// from, and then there is nothing to check: encode() is what refuses it.
template <std::meta::info Member, typename Value, typename Source>
[[nodiscard]] bool accepts_member(const Value& value, Source view,
                                  const decode_cursor& here) {
  if constexpr (kind_of(^^Value) == value_kind::optional) {
    return !value.has_value() || accepts_member<Member>(*value, view, here);
  } else if constexpr (kind_of(^^Value) == value_kind::indirect) {
    if constexpr (!std::same_as<Source, json_node>) {
      if (value.valueless_after_move()) {
        return true;
      }
    }
    return accepts_member<Member>(*value, view, here);
  } else {
    // A floating-point constraint is checked on the number as the parser read
    // it, and for a number that a string spelled, `view` is the string, so its
    // text is read once more. This is the one place that asks for what a string
    // spells before anything has failed, so it tests the option itself: a decode
    // that coerces no scalars goes straight on. No other constraint reads `view`.
    if constexpr (std::floating_point<Value> && std::same_as<Source, json_node> &&
                  !checked_constraints_v<Member>.empty()) {
      if (here.options().coerce_scalars) {
        if (const json_node spelled = spelled_scalar(view, here)) {
          view = spelled;
        }
      }
    }
    bool met = true;
    template for (constexpr std::meta::info constraint :
                  checked_constraints_v<Member>) {
      if (!meets<constraint>(value, view, here)) {
        met = false;
      }
    }
    return met && passes_checks<Member>(value, here);
  }
}

// Where `Member` stands among Type's fields in key order: its index in
// field_layouts_v<Type>. The member may be one that a class flattened into Type
// declares; it is still one field of Type, since an object that held such a
// class twice would have each of its keys twice, which the checks reject.
template <typename Type, std::meta::info Member>
inline constexpr std::size_t field_position_v = [] {
  std::size_t position = 0;
  while (field_layouts_v<Type>[position].member != Member) {
    ++position;
  }
  return position;
}();

// The values of Type's fields in an object, in key order as field_layouts_v<Type> is;
// a field the object lacks has a node with no value.
template <typename Type>
using field_values = std::array<json_node, field_layouts_v<Type>.size()>;

inline void report_unknown_key(const std::string_view key, const decode_cursor& here) {
  std::string reason{"contains unknown member "};
  append_json_string(reason, key);
  here.report(IssueKind::unknown_member, reason);
}

// Finds the value of each of Type's fields in `view`, an object, and reports
// each member of it that Type does not declare, in key order; returns false if
// there was one. The object's members and the type's fields are both in key
// order, so one pass over the two pairs them, where a search for each field and
// another for each member would compare keys many times over. Tagged is true
// when the object is a tagged variant alternative, whose discriminator member
// the variant has already read.
template <typename Type, bool Tagged>
[[nodiscard]] bool pair_members(const json_node view, const decode_cursor& here,
                                field_values<Type>& found) {
  bool known = true;
  const auto undeclared = [&here, &known](const std::string_view key) {
    if constexpr (!ignores_unknown(^^Type)) {
      if ((!Tagged || key != tag_key) && !here.stopped()) {
        report_unknown_key(key, here);
        known = false;
      }
    }
  };
  const std::size_t size = view.size();
  std::size_t position = 0;
  [[maybe_unused]] std::size_t slot = 0;
  template for (constexpr field_layout field : field_layouts_v<Type>) {
    // A member whose key sorts before this field's is no field's: every field
    // before this one has already met the keys up to its own.
    for (; position < size; ++position) {
      const std::string_view key = view.key_at(position);
      const auto order = key <=> field.key_view();
      if (std::is_gt(order)) {
        break;
      }
      if (std::is_eq(order)) {
        found[slot] = view.value_at(position);
        ++position;
        break;
      }
      undeclared(key);
    }
    ++slot;
  }
  for (; position < size; ++position) {
    undeclared(view.key_at(position));
  }
  return known;
}

// Reads one declared member of `object` from `found`, its value in the object
// being read or no value when that object lacks the member, and returns false
// if the member failed, missed a constraint, failed a check, or is required and
// absent. A member that keeps its default is the program's own value, so it is
// not checked. Root is the type of the JSON object, whose field the member is:
// the class of `object`, or one that flattens it.
//
// A defaulted argument is read as a value of its parameter's type, and the
// assignment engages the std::optional that stores it, also where that value is
// itself a std::optional that `null` left disengaged.
template <typename Root, std::meta::info Member, typename Owner>
[[nodiscard]] bool read_member(Owner& object, const json_node found,
                               const decode_cursor& parent) {
  constexpr field_layout field = field_layouts_v<Root>[field_position_v<Root, Member>];
  constexpr std::string_view key = field.key_view();
  using Value = std::remove_cv_t<typename[:value_type_of(Member):]>;
  const decode_cursor here = parent.member(key);
  if (found) {
    auto decoded = read_value<Value>(found, here);
    if (!decoded) {
      return false;
    }
    if constexpr (!checked_constraints_v<Member>.empty() ||
                  !validators_v<Member>.empty()) {
      if (!accepts_member<Member>(*decoded, found, here)) {
        return false;
      }
    }
    object.[:Member:] = std::move(*decoded);
  } else if constexpr (field.omit_null) {
    object.[:Member:].reset();
  } else if constexpr (field.required) {
    here.report(IssueKind::missing_member, "is a required member");
    return false;
  }
  return true;
}

// Whether `later` is declared after `member`, in the class that declares both.
consteval bool is_declared_after(const std::meta::info later,
                                 const std::meta::info member) {
  const auto members = data_members_of(std::meta::parent_of(member));
  return std::ranges::find(members, later) > std::ranges::find(members, member);
}

// Why a defaulted argument that the input left out is an issue, where the input
// gives the later one under `key`.
consteval std::string_view required_when_given(const std::string_view key) {
  std::string reason{"is required when "};
  append_json_string(reason, key);
  reason += " is given";
  return static_string(reason);
}

// Holds an argument object to the one rule that default arguments add. A call
// leaves an argument to its default by ending before it, so a default applies
// only when every argument after it is left out too. An input that leaves out
// `Member`, a defaulted argument of Root, and gives a later one has no call
// that honors both, and `Member` is reported as required there. Returns false
// if it was. No keyword of the subset that the schemas keep to states the rule,
// so the schema leaves it to this.
template <typename Root, std::meta::info Member>
[[nodiscard]] bool leaves_out_the_rest(const field_values<Root>& found,
                                       const decode_cursor& parent) {
  if (found[field_position_v<Root, Member>]) {
    return true;
  }
  template for (constexpr std::meta::info later : declared_members_v<Root>) {
    if constexpr (is_defaulted_argument(later) && is_declared_after(later, Member)) {
      if (found[field_position_v<Root, later>]) {
        constexpr std::string_view key =
            field_layouts_v<Root>[field_position_v<Root, Member>].key_view();
        constexpr std::string_view reason = required_when_given(
            field_layouts_v<Root>[field_position_v<Root, later>].key_view());
        parent.member(key).report(IssueKind::missing_member, reason);
        return false;
      }
    }
  }
  return true;
}

// Reads the members of `object`, a member flattened into the Root object being
// read, and returns false if it failed. Its members are fields of that JSON
// object, so they are read from `found`, the values of Root's fields, at the
// object's own path, and a flattened member among them likewise. The class of
// `object` has validators of its own, which run on it as read_object runs
// Root's: last, and only once every member of it is valid.
//
// The loop is read_object's, repeated rather than shared with it: GCC 16
// compiles a loop that the two share into other, larger code for a class that
// flattens nothing, and as it stands that class is read by the same
// instructions as before there were flattened members.
template <typename Root, typename Flattened>
[[nodiscard]] bool read_flattened(Flattened& object, const field_values<Root>& found,
                                  const decode_cursor& here) {
  bool valid = true;
  template for (constexpr std::meta::info member : declared_members_v<Flattened>) {
    if (here.stopped()) {
      return false;
    }
    if constexpr (is_flattened(member)) {
      if (!read_flattened<Root>(object.[:member:], found, here)) {
        valid = false;
      }
    } else if (!read_member<Root, member>(object, found[field_position_v<Root, member>],
                                          here)) {
      valid = false;
    }
  }
  return valid && passes_checks<^^Flattened>(object, here);
}

// Reads `view` into `object` and returns false if it failed. The members the
// type does not declare are reported first, and then its own are read in
// declaration order, a flattened member's where it stands. The class's
// validators run last, and see only an object whose every member is valid; in a
// recursive model each level's run once the levels inside it have passed theirs.
template <typename Type, bool Tagged>
[[nodiscard]] bool read_object(Type& object, json_node view,
                               const decode_cursor& here) {
  if (view.kind() != JsonKind::object) {
    view = spelled_structure(view, here, JsonKind::object);
    if (!view) {
      here.report(IssueKind::wrong_type, "must be an object");
      return false;
    }
  }
  field_values<Type> found{};
  bool valid = pair_members<Type, Tagged>(view, here, found);
  template for (constexpr std::meta::info member : declared_members_v<Type>) {
    if (here.stopped()) {
      return false;
    }
    // Only a synthesized argument object has such a member, and it flattens
    // none, so read_flattened() has no need of this.
    if constexpr (is_defaulted_argument(member)) {
      if (!leaves_out_the_rest<Type, member>(found, here)) {
        valid = false;
      }
    }
    if constexpr (is_flattened(member)) {
      if (!read_flattened<Type>(object.[:member:], found, here)) {
        valid = false;
      }
    } else if (!read_member<Type, member>(object, found[field_position_v<Type, member>],
                                          here)) {
      valid = false;
    }
  }
  return valid && passes_checks<^^Type>(object, here);
}

// An aggregate is read in place, so this is the one place that builds its
// result. The value is value-initialized, as is_storable checks: `Type value{}`
// would copy-initialize a member without an initializer from `{}`, which
// std::indirect's explicit default constructor does not allow.
template <typename Type, bool Tagged>
[[nodiscard]] decode_result<Type> read_in_place(const json_node view,
                                                const decode_cursor& here) {
  Type value = Type();
  if (!read_object<Type, Tagged>(value, view, here)) {
    return decode_failed<Type>();
  }
  return value;
}

// ---- Variants ----------------------------------------------------------------
//
// A variant is read as the alternative that the value's JSON kind selects
// (alternative_kind), and only as that one: no alternative is tried and given
// up, so the issues of a value are those of the alternative it is. The variant
// is built from that alternative rather than read in place, which would
// default-construct its first alternative only to replace it, so it builds its
// failures itself.

// Reads `view` as the alternative of Variant that `Alternative` reflects. A
// tagged alternative is the object with its tag, a std::monostate has nothing
// to read, and any other is read as its type is anywhere.
template <typename Variant, std::meta::info Alternative>
[[nodiscard]] decode_result<Variant> read_alternative(const json_node view,
                                                      const decode_cursor& here) {
  using Type = typename[:Alternative:];
  constexpr auto held = [](Type&& value) {
    return Variant{std::in_place_type<Type>, std::move(value)};
  };
  if constexpr (alternative_kind_of(Alternative) == alternative_kind::null) {
    return decode_result<Variant>{std::in_place, std::in_place_type<Type>};
  } else if constexpr (is_tagged_alternative(Alternative)) {
    return read_in_place<Type, true>(view, here).transform(held);
  } else {
    return read_value<Type>(view, here).transform(held);
  }
}

// Reads `view`, an object, as the tagged alternative of Variant that its tag
// names. A missing or unknown tag leaves no alternative to check the object
// against, so it is the variant's only issue.
template <typename Variant>
[[nodiscard]] decode_result<Variant> read_tagged(const json_node view,
                                                 const decode_cursor& here) {
  static constexpr std::string_view tags = tag_names(^^Variant);
  const decode_cursor tag = here.member(tag_key);
  const auto discriminator = view.find(tag_key);
  if (!discriminator) {
    tag.report(IssueKind::missing_tag,
               std::string{"is a required member; must be one of: "} +
                   std::string{tags});
    return decode_failed<Variant>();
  }

  const auto selected = discriminator.string();
  template for (constexpr std::meta::info alternative : alternatives_v<Variant>) {
    if constexpr (is_tagged_alternative(alternative)) {
      if (selected == *tag_of(alternative)) {
        return read_alternative<Variant, alternative>(view, here);
      }
    }
  }
  tag.report(IssueKind::unknown_tag,
             std::string{"must be one of: "} + std::string{tags});
  return decode_failed<Variant>();
}

// Reads `view`, a value of `Kind`, as the alternative of Variant that has the
// kind. A variant without one rejects the value, and tells the kinds it does
// accept.
template <typename Variant, alternative_kind Kind>
[[nodiscard]] decode_result<Variant> read_kind(const json_node view,
                                               const decode_cursor& here) {
  constexpr std::meta::info alternative = kind_alternative_v<Variant, Kind>;
  if constexpr (!has_kind_v<Variant, Kind>) {
    static constexpr std::string_view reason = accepted_kinds_reason(^^Variant);
    here.report(IssueKind::wrong_type, reason);
    return decode_failed<Variant>();
  } else if constexpr (is_tagged_alternative(alternative)) {
    return read_tagged<Variant>(view, here);
  } else {
    return read_alternative<Variant, alternative>(view, here);
  }
}

// Whether `view`, a JSON integer, is a value of Integer.
template <typename Integer>
[[nodiscard]] bool holds_integer(const json_node view) noexcept {
  if (const auto value = view.signed_integer()) {
    return std::in_range<Integer>(*value);
  }
  return std::in_range<Integer>(*view.unsigned_integer());
}

// Reads `view`, a JSON integer, as the integer alternative of Variant. An
// integer past that alternative's range is a number still, so a floating-point
// alternative takes it, as it takes every integer of a variant with no integer
// alternative. Without one, the integer is held to the range.
template <typename Variant>
[[nodiscard]] decode_result<Variant> read_integer_kind(const json_node view,
                                                       const decode_cursor& here) {
  using enum alternative_kind;
  if constexpr (has_kind_v<Variant, integer> && has_kind_v<Variant, number>) {
    if (!holds_integer<typename[:kind_alternative_v<Variant, integer>:]>(view)) {
      return read_kind<Variant, number>(view, here);
    }
  }
  return read_kind<Variant, has_kind_v<Variant, integer> ? integer : number>(view,
                                                                             here);
}

// The value of a kind that an alternative of Variant has, which the text of
// `view`, a string, spells, when the decode coerces that kind; otherwise no
// value. Text spells one value, so the first that reads it is the only one.
template <typename Variant>
[[nodiscard]] json_node spelled_alternative(const json_node view,
                                            const decode_cursor& here) {
  using enum alternative_kind;
  json_node spelled{};
  if constexpr (has_kind_v<Variant, boolean> || has_kind_v<Variant, integer> ||
                has_kind_v<Variant, number>) {
    spelled = spelled_scalar(view, here);
  }
  if constexpr (has_kind_v<Variant, array>) {
    if (!spelled) {
      spelled = spelled_structure(view, here, JsonKind::array);
    }
  }
  if constexpr (has_kind_v<Variant, object>) {
    if (!spelled) {
      spelled = spelled_structure(view, here, JsonKind::object);
    }
  }
  return spelled;
}

// The one switch on the value's kind. A string is the string alternative
// whatever its text spells, as a std::string member takes any string. Only a
// variant without one can take a string for the value it spells, and then reads
// that value as if it stood in the string's place. A string that spells no
// value an alternative could be stays a string, which the variant rejects.
template <typename Variant>
[[nodiscard]] decode_result<Variant> read_variant(json_node view,
                                                  const decode_cursor& here) {
  using enum alternative_kind;
  JsonKind kind = view.kind();
  if constexpr (!has_kind_v<Variant, string>) {
    if (kind == JsonKind::string) {
      if (const json_node spelled = spelled_alternative<Variant>(view, here)) {
        view = spelled;
        kind = view.kind();
      }
    }
  }
  switch (kind) {
  case JsonKind::null:
    return read_kind<Variant, null>(view, here);
  case JsonKind::boolean:
    return read_kind<Variant, boolean>(view, here);
  case JsonKind::signed_integer:
  case JsonKind::unsigned_integer:
    return read_integer_kind<Variant>(view, here);
  case JsonKind::number:
    return read_kind<Variant, number>(view, here);
  case JsonKind::string:
    return read_kind<Variant, string>(view, here);
  case JsonKind::array:
    return read_kind<Variant, array>(view, here);
  case JsonKind::object:
    return read_kind<Variant, object>(view, here);
  }
  std::unreachable();
}

template <typename Optional>
[[nodiscard]] decode_result<Optional> read_optional(const json_node view,
                                                    const decode_cursor& here) {
  if (view.kind() == JsonKind::null) {
    return Optional{std::nullopt};
  }
  return read_value<typename Optional::value_type>(view, here)
      .transform([](typename Optional::value_type&& value) {
        return Optional{std::move(value)};
      });
}

template <typename Indirect>
[[nodiscard]] decode_result<Indirect> read_indirect(const json_node view,
                                                    const decode_cursor& here) {
  using Owned = typename Indirect::value_type;
  return read_value<Owned>(view, here).transform([](Owned&& value) {
    return Indirect{std::in_place, std::move(value)};
  });
}

template <typename Vector>
[[nodiscard]] decode_result<Vector> read_vector(json_node view,
                                                const decode_cursor& here) {
  if (view.kind() != JsonKind::array) {
    view = spelled_structure(view, here, JsonKind::array);
    if (!view) {
      return here.fail(IssueKind::wrong_type, "must be an array");
    }
  }
  Vector values{};
  const std::size_t size = view.size();
  values.reserve(size);
  bool valid = true;
  for (std::size_t index = 0; index < size && !here.stopped(); ++index) {
    if (auto decoded = read_value<typename Vector::value_type>(view.at(index),
                                                               here.element(index))) {
      values.push_back(std::move(*decoded));
    } else {
      valid = false;
    }
  }
  if (!valid) {
    return std::unexpected(decode_failure::reported);
  }
  return values;
}

template <typename Array>
[[nodiscard]] decode_result<Array> read_array(json_node view,
                                              const decode_cursor& here) {
  constexpr std::string_view reason = "must be an array of the declared fixed size";
  // Value-initialized for the reason read_in_place's value is.
  Array values = Array();
  if (view.kind() != JsonKind::array) {
    view = spelled_structure(view, here, JsonKind::array);
    if (!view) {
      return here.fail(IssueKind::wrong_type, reason);
    }
  }
  if (view.size() != values.size()) {
    return here.fail(IssueKind::wrong_size, reason);
  }
  bool valid = true;
  for (std::size_t index = 0; index < values.size() && !here.stopped(); ++index) {
    if (auto decoded = read_value<typename Array::value_type>(view.at(index),
                                                              here.element(index))) {
      values[index] = std::move(*decoded);
    } else {
      valid = false;
    }
  }
  if (!valid) {
    return std::unexpected(decode_failure::reported);
  }
  return values;
}

// Every member of the object is an entry, read in key order, which is the order
// the document holds them in and so the order their issues are reported in. The
// document has already kept only the last of its members of one key. Keys that
// differ as strings can still be one key to a map with a comparator or an
// equality of its own, such as one that ignores case; the later entry then
// replaces the earlier one's value, as the later of two equal keys does.
template <typename Map>
[[nodiscard]] decode_result<Map> read_map(json_node view, const decode_cursor& here) {
  if (view.kind() != JsonKind::object) {
    view = spelled_structure(view, here, JsonKind::object);
    if (!view) {
      return here.fail(IssueKind::wrong_type, "must be an object");
    }
  }
  Map values{};
  const std::size_t size = view.size();
  if constexpr (requires { values.reserve(size); }) {
    values.reserve(size);
  }
  bool valid = true;
  for (std::size_t index = 0; index < size && !here.stopped(); ++index) {
    const std::string_view key = view.key_at(index);
    if (auto decoded = read_value<typename Map::mapped_type>(view.value_at(index),
                                                             here.entry(key))) {
      // The keys arrive in the order of a std::map of std::less, where the hint
      // makes each insertion constant time. To any other map it is only a hint.
      values.insert_or_assign(values.end(), std::string{key}, std::move(*decoded));
    } else {
      valid = false;
    }
  }
  if (!valid) {
    return std::unexpected(decode_failure::reported);
  }
  return values;
}

// Booleans, strings, and raw JSON: the leaves that need no numeric range.
template <typename Type>
[[nodiscard]] decode_result<Type> read_plain_leaf(json_node view,
                                                  const decode_cursor& here) {
  if constexpr (std::same_as<Type, bool>) {
    if (view.kind() != JsonKind::boolean) {
      view = spelled_scalar(view, here);
      // What a string spells is a number where it is not a boolean.
      if (!view || view.kind() != JsonKind::boolean) {
        return here.fail(IssueKind::wrong_type, "must be a boolean");
      }
    }
    return *view.boolean();
  } else if constexpr (std::same_as<Type, std::string>) {
    if (view.kind() != JsonKind::string) {
      return here.fail(IssueKind::wrong_type, "must be a string");
    }
    return std::string{*view.string()};
  } else {
    return view.to_json();
  }
}

template <typename Type>
decode_result<Type> read_value(const json_node view, const decode_cursor& here) {
  constexpr auto kind = kind_of(^^Type);
  if constexpr (kind == value_kind::integer) {
    return read_integer<Type>(view, here);
  } else if constexpr (kind == value_kind::floating) {
    return read_float<Type>(view, here);
  } else if constexpr (kind == value_kind::enumeration) {
    return read_enum<Type>(view, here);
  } else if constexpr (kind == value_kind::optional) {
    return read_optional<Type>(view, here);
  } else if constexpr (kind == value_kind::indirect) {
    return read_indirect<Type>(view, here);
  } else if constexpr (kind == value_kind::vector) {
    return read_vector<Type>(view, here);
  } else if constexpr (kind == value_kind::array) {
    return read_array<Type>(view, here);
  } else if constexpr (kind == value_kind::map) {
    return read_map<Type>(view, here);
  } else if constexpr (kind == value_kind::variant) {
    return read_variant<Type>(view, here);
  } else if constexpr (kind == value_kind::aggregate) {
    return read_in_place<Type, false>(view, here);
  } else {
    return read_plain_leaf<Type>(view, here);
  }
}

// Decodes a value checked against a family, reporting every issue it finds with
// a schema-derived model_message. `options` are the public decode()'s.
template <typename Type>
[[nodiscard]] Result<Type> decode(const JsonView& view,
                                  const DecodeOptions& options = {}) {
  decode_report report{.options = options};
  return read_value<Type>(json_node{view}, decode_cursor{report})
      .transform_error(
          [&report](decode_failure) { return decode_error(std::move(report)); });
}

} // namespace scrydantic::detail
