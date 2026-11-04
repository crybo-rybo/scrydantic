#pragma once

#include <array>
#include <bit>
#include <compare>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <limits>
#include <meta>
#include <optional>
#include <scrydantic/annotations.hpp>
#include <scrydantic/detail/json_number.hpp>
#include <scrydantic/detail/json_string.hpp>
#include <scrydantic/detail/model.hpp>
#include <scrydantic/error.hpp>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

// Constraint annotations, resolved against the type of the member that carries
// them. One resolution serves the family checks, which reject a constraint that
// cannot hold; the schema generator, which writes it as JSON Schema keywords; and
// the decoder, which checks it and reports a value past it in those keywords'
// words. The schema and the check therefore bound the same values. Everything
// here assumes the member's type has passed its family check. The validators
// that check annotations name, which no schema states, are resolved here too.
namespace scrydantic::detail {

// A constraint annotation type, and the name a diagnostic gives it. The display
// string of a pattern would name its extent, which says nothing to the reader.
struct constraint_annotation {
  std::meta::info what{};
  std::string_view name{};
};

inline constexpr std::array constraint_annotations{
    constraint_annotation{.what = ^^range, .name = "scrydantic::range"},
    constraint_annotation{.what = ^^multiple_of, .name = "scrydantic::multiple_of"},
    constraint_annotation{.what = ^^length, .name = "scrydantic::length"},
    constraint_annotation{.what = ^^items, .name = "scrydantic::items"},
    constraint_annotation{.what = ^^pattern, .name = "scrydantic::pattern"},
};

// The name of a constraint annotation, or empty for any other annotation.
consteval std::string_view constraint_name(const std::meta::info annotation) {
  for (const auto& constraint : constraint_annotations) {
    if (annotation_matches(annotation, constraint.what)) {
      return constraint.name;
    }
  }
  return {};
}

consteval bool is_constraint(const std::meta::info annotation) {
  return !constraint_name(annotation).empty();
}

// The bounds of a length or an items annotation, which have one shape.
consteval std::pair<std::size_t, std::size_t>
size_limits(const std::meta::info annotation) {
  if (annotation_matches(annotation, ^^length)) {
    const auto size = std::meta::extract<length>(annotation);
    return {size.min, size.max};
  }
  const auto size = std::meta::extract<items>(annotation);
  return {size.min, size.max};
}

// The type a member's constraints bound: the member's own, or the value type of
// an optional member, whose constraints bound the engaged value, each seen
// through any std::indirect, which the codec treats as the value it owns.
consteval std::meta::info constrained_type(const std::meta::info member) {
  const auto type = owned_type(std::meta::type_of(member));
  return kind_of(type) == value_kind::optional ? owned_type(element_of(type)) : type;
}

// The constraint annotations a decoder checks, in declaration order. A pattern
// is the schema's alone.
consteval std::vector<std::meta::info>
checked_constraints_of(const std::meta::info member) {
  std::vector<std::meta::info> constraints{};
  for (const auto annotation : std::meta::annotations_of(member)) {
    if (is_constraint(annotation) && !annotation_matches(annotation, ^^pattern)) {
      constraints.push_back(annotation);
    }
  }
  return constraints;
}

template <std::meta::info Member>
inline constexpr auto checked_constraints_v =
    std::define_static_array(checked_constraints_of(Member));

// ---- Numbers -----------------------------------------------------------------

// What a bound needs to know about an integer or floating-point type.
struct numeric_type {
  bool floating{};
  // A float rather than a double.
  bool single{};
  // An integer type's largest value, and the magnitude of its lowest.
  std::uint64_t maximum{};
  std::uint64_t lowest_magnitude{};
};

consteval numeric_type numeric_type_of(const std::meta::info type) {
  if (kind_of(type) == value_kind::floating) {
    return {.floating = true, .single = plain_type(type) == (^^float)};
  }
  const bool is_signed = std::meta::is_signed_type(type);
  const auto magnitude_bits = (std::meta::size_of(type) * 8U) - (is_signed ? 1U : 0U);
  const auto maximum = magnitude_bits == 64U
                           ? std::numeric_limits<std::uint64_t>::max()
                           : (std::uint64_t{1} << magnitude_bits) - 1U;
  // The lowest value of a signed type is -(maximum + 1), whose magnitude an
  // unsigned holds.
  return {.maximum = maximum, .lowest_magnitude = is_signed ? maximum + 1U : 0U};
}

consteval bool is_set(const bound value) { return value.form != bound_form::none; }

consteval bound integer_bound(const bool negative, const std::uint64_t magnitude) {
  bound value{};
  value.form = bound_form::integer;
  value.negative = negative && magnitude != 0U;
  value.magnitude = magnitude;
  return value;
}

consteval bound floating_bound(const double number) {
  bound value{};
  value.form = bound_form::floating;
  value.floating = number;
  return value;
}

// By the bits, since arithmetic that makes an infinity or a NaN is not a
// constant expression: the exponent of a value that is not finite is all ones.
consteval bool is_finite(const double number) {
  constexpr std::uint64_t exponent = std::uint64_t{0x7FF} << 52U;
  return (std::bit_cast<std::uint64_t>(number) & exponent) != exponent;
}

// A bound as JSON spells it: an integer's digits, or a double's canonical
// spelling, which for a float is the spelling of the double it is. JSON cannot
// spell a value that is not finite, which only a diagnostic names.
consteval std::string bound_text(const bound value) {
  if (value.form == bound_form::integer) {
    return (value.negative ? "-" : "") + decimal(value.magnitude);
  }
  const double number = value.floating;
  if (number != number) {
    return "NaN";
  }
  if (!is_finite(number)) {
    return number < 0.0 ? "-infinity" : "infinity";
  }
  std::string text{};
  append_canonical_double(text, number);
  return text;
}

// `value` as an integer of `type`, or why it is not one.
consteval std::expected<bound, std::string>
integer_in(bound value, const numeric_type type, const std::string& name) {
  if (value.form == bound_form::floating) {
    const double number = value.floating;
    const double magnitude = number < 0.0 ? -number : number;
    if (!is_finite(number)) {
      return std::unexpected("is not finite");
    }
    // 2^64 is past every integer type, and every double from 2^53 up is whole.
    if (magnitude >= 0x1p64) {
      return std::unexpected("is outside the range of " + name);
    }
    const auto whole = static_cast<std::uint64_t>(magnitude);
    if (static_cast<double>(whole) != magnitude) {
      return std::unexpected("is not an integer");
    }
    value = integer_bound(number < 0.0, whole);
  }
  if (value.magnitude > (value.negative ? type.lowest_magnitude : type.maximum)) {
    return std::unexpected("is outside the range of " + name);
  }
  return value;
}

// `value` as a floating-point value of `type`, or why it is not exactly one.
consteval std::expected<bound, std::string>
floating_in(const bound value, const numeric_type type, const std::string& name) {
  double number = value.floating;
  if (value.form == bound_form::integer) {
    // A conversion rounds an integer that no double holds, and 2^64, where the
    // largest magnitudes round to, is past every integer.
    const auto converted = static_cast<double>(value.magnitude);
    if (converted >= 0x1p64 ||
        static_cast<std::uint64_t>(converted) != value.magnitude) {
      return std::unexpected("is not exactly a " + name);
    }
    number = value.negative ? -converted : converted;
  }
  if (!is_finite(number)) {
    return std::unexpected("is not finite");
  }
  const double magnitude = number < 0.0 ? -number : number;
  if (type.single &&
      (magnitude > static_cast<double>(std::numeric_limits<float>::max()) ||
       static_cast<double>(static_cast<float>(number)) != number)) {
    return std::unexpected("is not exactly a " + name);
  }
  return floating_bound(number);
}

// `value` as a value of `type`: an integer for an integer type and a double for
// a floating-point one, or why it is not exactly a value of `type`.
consteval std::expected<bound, std::string> bound_in(const bound value,
                                                     const std::meta::info type) {
  const auto numeric = numeric_type_of(type);
  const auto name = display(type);
  return numeric.floating ? floating_in(value, numeric, name)
                          : integer_in(value, numeric, name);
}

// A bound that has passed bound_in, as a value of `type`.
consteval bound normalized(const bound value, const std::meta::info type) {
  return *bound_in(value, type);
}

// Both bounds are values of one type, so they have one form.
consteval std::partial_ordering compare(const bound left, const bound right) {
  if (left.form == bound_form::floating) {
    return left.floating <=> right.floating;
  }
  if (left.negative != right.negative) {
    return left.negative ? std::partial_ordering::less : std::partial_ordering::greater;
  }
  return left.negative ? right.magnitude <=> left.magnitude
                       : left.magnitude <=> right.magnitude;
}

// The value of `type` just above `value`, which is below the type's largest.
consteval bound next_above(const bound value, const numeric_type type) {
  if (!type.floating) {
    return value.negative ? integer_bound(true, value.magnitude - 1U)
                          : integer_bound(false, value.magnitude + 1U);
  }
  // Finite floating-point values of one sign are ordered as their bit patterns,
  // and the next above either zero is the smallest subnormal.
  if (type.single) {
    const auto number = static_cast<float>(value.floating);
    const auto bits = std::bit_cast<std::uint32_t>(number);
    return floating_bound(static_cast<double>(
        number == 0.0F ? std::numeric_limits<float>::denorm_min()
                       : std::bit_cast<float>(number > 0.0F ? bits + 1U : bits - 1U)));
  }
  const auto bits = std::bit_cast<std::uint64_t>(value.floating);
  return floating_bound(
      value.floating == 0.0
          ? std::numeric_limits<double>::denorm_min()
          : std::bit_cast<double>(value.floating > 0.0 ? bits + 1U : bits - 1U));
}

// ---- Ranges ------------------------------------------------------------------

// One end of a range, as a value of the member's type.
struct range_end {
  bound value{};
  bool exclusive{};
  // The range field that set it, for diagnostics.
  std::string_view field{};
};

// The lower end a range sets, if it sets one.
consteval std::optional<range_end> lower_end(const range& bounds) {
  if (is_set(bounds.min)) {
    return range_end{.value = bounds.min, .field = "min"};
  }
  if (is_set(bounds.exclusive_min)) {
    return range_end{
        .value = bounds.exclusive_min, .exclusive = true, .field = "exclusive_min"};
  }
  return std::nullopt;
}

consteval std::optional<range_end> upper_end(const range& bounds) {
  if (is_set(bounds.max)) {
    return range_end{.value = bounds.max, .field = "max"};
  }
  if (is_set(bounds.exclusive_max)) {
    return range_end{
        .value = bounds.exclusive_max, .exclusive = true, .field = "exclusive_max"};
  }
  return std::nullopt;
}

// A range's ends as values of `type`, in the order they bound it. An end the
// range leaves open is the type's own: an integer type's limit, or the largest
// finite float or double, since decoding admits no other.
consteval std::array<range_end, 2> range_ends(const range& bounds,
                                              const std::meta::info type) {
  const auto numeric = numeric_type_of(type);
  const double largest = numeric.single
                             ? static_cast<double>(std::numeric_limits<float>::max())
                             : std::numeric_limits<double>::max();
  auto lower = lower_end(bounds).value_or(range_end{
      .value = numeric.floating ? floating_bound(-largest)
                                : integer_bound(true, numeric.lowest_magnitude)});
  auto upper = upper_end(bounds).value_or(
      range_end{.value = numeric.floating ? floating_bound(largest)
                                          : integer_bound(false, numeric.maximum)});
  lower.value = normalized(lower.value, type);
  upper.value = normalized(upper.value, type);
  return {lower, upper};
}

// Whether a value of `type` lies between two ends that do not cross.
consteval bool admits_value(const range_end& lower, const range_end& upper,
                            const std::meta::info type) {
  if (compare(lower.value, upper.value) == 0) {
    return !lower.exclusive && !upper.exclusive;
  }
  if (!lower.exclusive || !upper.exclusive) {
    return true;
  }
  return compare(next_above(lower.value, numeric_type_of(type)), upper.value) < 0;
}

consteval std::string range_bounds_problem(const range& bounds,
                                           const std::meta::info type) {
  for (const auto& [field, value] :
       {std::pair{"min", bounds.min}, std::pair{"exclusive_min", bounds.exclusive_min},
        std::pair{"max", bounds.max},
        std::pair{"exclusive_max", bounds.exclusive_max}}) {
    if (!is_set(value)) {
      continue;
    }
    if (const auto converted = bound_in(value, type); !converted) {
      return std::string{"carries a scrydantic::range whose "} + field + ", " +
             bound_text(value) + ", " + converted.error();
    }
  }
  return {};
}

consteval std::string range_problem(const range& bounds, const std::meta::info type) {
  const auto lower = lower_end(bounds);
  const auto upper = upper_end(bounds);
  if (!lower && !upper) {
    return "carries a scrydantic::range that sets no bound";
  }
  if (is_set(bounds.min) && is_set(bounds.exclusive_min)) {
    return "carries a scrydantic::range that sets both min and exclusive_min";
  }
  if (is_set(bounds.max) && is_set(bounds.exclusive_max)) {
    return "carries a scrydantic::range that sets both max and exclusive_max";
  }
  if (auto text = range_bounds_problem(bounds, type); !text.empty()) {
    return text;
  }
  const auto [low, high] = range_ends(bounds, type);
  if (lower && upper && compare(low.value, high.value) > 0) {
    return "carries a scrydantic::range whose " + owned_text(low.field) + ", " +
           bound_text(low.value) + ", exceeds its " + owned_text(high.field) + ", " +
           bound_text(high.value);
  }
  if (!admits_value(low, high, type)) {
    return "carries a scrydantic::range that no value of " + display(type) +
           " satisfies";
  }
  return {};
}

// ---- Multiples ---------------------------------------------------------------

// Whether a positive finite double's shortest decimal spelling, which the
// schema writes, is its exact value, so that decimal and binary multiples of it
// are the same numbers.
consteval bool is_exact_decimal(const double factor) {
  return compare(shortest_decimal(factor), exact_decimal_of(factor)) == 0;
}

consteval std::string multiple_problem(const multiple_of& multiple,
                                       const std::meta::info type) {
  if (!is_set(multiple.value)) {
    return "carries a scrydantic::multiple_of that sets no value";
  }
  const auto prefix = "carries a scrydantic::multiple_of whose value, " +
                      bound_text(multiple.value) + ", ";
  const auto factor = bound_in(multiple.value, type);
  if (!factor) {
    return prefix + factor.error();
  }
  if (factor->form == bound_form::floating
          ? factor->floating <= 0.0
          : factor->negative || factor->magnitude == 0U) {
    return prefix + "is not positive";
  }
  if (factor->form == bound_form::floating && !is_exact_decimal(factor->floating)) {
    return prefix +
           "is not exactly that decimal in binary, so the schema's decimal multiples "
           "of it are not the decoder's binary ones; use a binary fraction such as "
           "0.5, or an integer member that counts smaller units";
  }
  return {};
}

// ---- Sizes -------------------------------------------------------------------

consteval std::string size_problem(const std::string_view annotation,
                                   const std::size_t min, const std::size_t max) {
  if (min == 0U && max == std::numeric_limits<std::size_t>::max()) {
    return "carries a " + owned_text(annotation) + " that sets no bound";
  }
  if (min > max) {
    return "carries a " + owned_text(annotation) + " whose min, " + decimal(min) +
           ", exceeds its max, " + decimal(max);
  }
  return {};
}

// ---- Diagnostics -------------------------------------------------------------

// Why a constraint annotation does not apply to a member of `declared` type,
// whose constrained type is `type`; empty when it does.
consteval std::string applicability_problem(const std::meta::info annotation,
                                            const std::meta::info declared,
                                            const std::meta::info type) {
  const auto kind = kind_of(type);
  const auto name = owned_text(constraint_name(annotation));
  if (annotation_matches(annotation, ^^items) && kind == value_kind::array) {
    return "carries scrydantic::items, which does not apply to " + display(declared) +
           ", whose size is fixed";
  }
  bool applies = kind == value_kind::string;
  std::string_view members = "std::string";
  if (annotation_matches(annotation, ^^range) ||
      annotation_matches(annotation, ^^multiple_of)) {
    applies = kind == value_kind::integer || kind == value_kind::floating;
    members = "integer and floating-point";
  } else if (annotation_matches(annotation, ^^items)) {
    applies = kind == value_kind::vector;
    members = "std::vector";
  }
  if (applies) {
    return {};
  }
  return "carries " + name + ", which applies to " + owned_text(members) +
         " members, not to " + display(declared);
}

consteval std::string annotation_problem(const std::meta::info annotation,
                                         const std::meta::info member) {
  const auto type = constrained_type(member);
  if (auto text = applicability_problem(annotation, std::meta::type_of(member), type);
      !text.empty()) {
    return text;
  }
  if (annotation_matches(annotation, ^^range)) {
    return range_problem(std::meta::extract<range>(annotation), type);
  }
  if (annotation_matches(annotation, ^^multiple_of)) {
    return multiple_problem(std::meta::extract<multiple_of>(annotation), type);
  }
  if (annotation_matches(annotation, ^^pattern)) {
    return {};
  }
  const auto [min, max] = size_limits(annotation);
  return size_problem(constraint_name(annotation), min, max);
}

// Why the constraint annotations of `member` cannot hold; empty when they can.
consteval std::string constraint_problem(const std::meta::info member) {
  for (const auto& constraint : constraint_annotations) {
    if (annotation_count(member, constraint.what) > 1) {
      return "has more than one " + owned_text(constraint.name) + " annotation";
    }
  }
  for (const auto annotation : std::meta::annotations_of(member)) {
    if (is_constraint(annotation)) {
      if (auto text = annotation_problem(annotation, member); !text.empty()) {
        return text;
      }
    }
  }
  return {};
}

// ---- Schema keywords ---------------------------------------------------------

// One keyword of a schema object and its value, as JSON text.
struct schema_keyword {
  std::string key{};
  std::string value{};
};

consteval void append_range_keywords(std::vector<schema_keyword>& keywords,
                                     const range& bounds, const std::meta::info type) {
  if (const auto lower = lower_end(bounds)) {
    keywords.push_back({.key = lower->exclusive ? "exclusiveMinimum" : "minimum",
                        .value = bound_text(normalized(lower->value, type))});
  }
  if (const auto upper = upper_end(bounds)) {
    keywords.push_back({.key = upper->exclusive ? "exclusiveMaximum" : "maximum",
                        .value = bound_text(normalized(upper->value, type))});
  }
}

consteval void append_size_keywords(std::vector<schema_keyword>& keywords,
                                    const std::string_view noun, const std::size_t min,
                                    const std::size_t max) {
  if (min != 0U) {
    keywords.push_back({.key = "min" + owned_text(noun), .value = decimal(min)});
  }
  if (max != std::numeric_limits<std::size_t>::max()) {
    keywords.push_back({.key = "max" + owned_text(noun), .value = decimal(max)});
  }
}

consteval void append_keywords_of(std::vector<schema_keyword>& keywords,
                                  const std::meta::info annotation,
                                  const std::meta::info type) {
  if (annotation_matches(annotation, ^^range)) {
    append_range_keywords(keywords, std::meta::extract<range>(annotation), type);
  } else if (annotation_matches(annotation, ^^multiple_of)) {
    keywords.push_back({.key = "multipleOf",
                        .value = bound_text(normalized(
                            std::meta::extract<multiple_of>(annotation).value, type))});
  } else if (annotation_matches(annotation, ^^pattern)) {
    std::string text{};
    append_json_string(text, annotation_text(annotation));
    keywords.push_back({.key = "pattern", .value = std::move(text)});
  } else {
    const auto [min, max] = size_limits(annotation);
    append_size_keywords(keywords,
                         annotation_matches(annotation, ^^length) ? "Length" : "Items",
                         min, max);
  }
}

// The keywords that the constraints of `member` add to the schema of its
// constrained value, in no particular order.
consteval std::vector<schema_keyword>
constraint_keywords(const std::meta::info member) {
  std::vector<schema_keyword> keywords{};
  for (const auto annotation : std::meta::annotations_of(member)) {
    if (is_constraint(annotation)) {
      append_keywords_of(keywords, annotation, constrained_type(member));
    }
  }
  return keywords;
}

// ---- Decoding ----------------------------------------------------------------

// A floating-point member is checked twice: the number as the parser read it,
// which is what the schema bounds, and the value once converted to the member's
// type, which is what the host gets. Conversion rounds, and can carry a number
// that the schema admits onto an exclusive bound or off a multiple, so that
// second miss has a reason of its own: the bound's, and that it holds even once
// rounded. The model's own number does meet the bound, so the bound's reason
// alone would only make it send that number again. The words are the schema's
// and Scrydantic's, and none of them the host's.
consteval std::string rounded_reason(const std::string& reason,
                                     const std::meta::info type) {
  return reason + (numeric_type_of(type).single
                       ? " even once rounded to single precision"
                       : " even once rounded to double precision");
}

// One end of a range as decoding checks it: its value in the constrained type,
// which way and how it bounds, the reason a value past it is reported with, the
// keyword's meaning in words, and for a floating-point member the reason for a
// number that only its conversion carries onto the end.
struct checked_end {
  bool present{};
  bool lower{};
  bool exclusive{};
  bound value{};
  std::string_view reason{};
  std::string_view rounded_reason{};
};

// The two ends of a range on a value of `type`.
struct checked_range {
  checked_end lower{};
  checked_end upper{};
};

consteval checked_end checked_end_of(const std::optional<range_end>& end,
                                     const std::meta::info type, const bool lower) {
  if (!end) {
    return {};
  }
  const auto value = normalized(end->value, type);
  const auto words =
      lower ? (end->exclusive ? "must be greater than " : "must be at least ")
            : (end->exclusive ? "must be less than " : "must be at most ");
  const auto reason = words + bound_text(value);
  return checked_end{
      .present = true,
      .lower = lower,
      .exclusive = end->exclusive,
      .value = value,
      .reason = static_string(reason),
      .rounded_reason = numeric_type_of(type).floating
                            ? static_string(rounded_reason(reason, type))
                            : std::string_view{},
  };
}

consteval checked_range checked_range_of(const std::meta::info annotation,
                                         const std::meta::info type) {
  const auto bounds = std::meta::extract<range>(annotation);
  return {.lower = checked_end_of(lower_end(bounds), type, true),
          .upper = checked_end_of(upper_end(bounds), type, false)};
}

// A multiple_of as decoding checks it. For a floating-point member the factor is
// also kept as an odd integer times a power of two, so that an integer input,
// which the parser reads exactly, is tested exactly however large it is.
struct checked_multiple {
  bound value{};
  std::uint64_t odd{};
  int two_exponent{};
  std::string_view reason{};
  std::string_view rounded_reason{};
};

consteval checked_multiple checked_multiple_of(const std::meta::info annotation,
                                               const std::meta::info type) {
  const auto value =
      normalized(std::meta::extract<multiple_of>(annotation).value, type);
  const auto reason = "must be a multiple of " + bound_text(value);
  checked_multiple multiple{.value = value, .reason = static_string(reason)};
  if (value.form == bound_form::floating) {
    auto binary = binary_value_of(value.floating);
    for (; binary.mantissa % 2U == 0U; binary.mantissa /= 2U) {
      ++binary.exponent;
    }
    multiple.odd = binary.mantissa;
    multiple.two_exponent = binary.exponent;
    multiple.rounded_reason = static_string(rounded_reason(reason, type));
  }
  return multiple;
}

// The size limits of a length or items annotation. A limit at its default bounds
// nothing and is never reported.
struct checked_size {
  std::size_t min{};
  std::size_t max{};
  std::string_view min_reason{};
  std::string_view max_reason{};
};

consteval std::string counted(const std::size_t count, const std::string_view noun) {
  return decimal(count) + " " + owned_text(noun) + (count == 1U ? "" : "s");
}

consteval checked_size checked_size_of(const std::meta::info annotation) {
  const auto [min, max] = size_limits(annotation);
  const auto noun = annotation_matches(annotation, ^^length) ? "character" : "item";
  return {.min = min,
          .max = max,
          .min_reason = static_string("must have at least " + counted(min, noun)),
          .max_reason = static_string("must have at most " + counted(max, noun))};
}

// A bound as a value of `Number`, which holds it exactly.
template <typename Number> consteval Number bound_value(const bound value) {
  if constexpr (std::floating_point<Number>) {
    return static_cast<Number>(value.floating);
  } else if (value.negative) {
    // Through magnitude - 1, so that the lowest int64 does not overflow.
    return static_cast<Number>(-static_cast<std::int64_t>(value.magnitude - 1U) - 1);
  } else {
    return static_cast<Number>(value.magnitude);
  }
}

// ---- Validators --------------------------------------------------------------

// The validators that the check annotations of `entity`, a data member or a
// class, name, in declaration order: each the reflection of a function pointer.
consteval std::vector<std::meta::info> validators_of(const std::meta::info entity) {
  std::vector<std::meta::info> validators{};
  for (const auto annotation : std::meta::annotations_of(entity)) {
    if (annotation_matches(annotation, ^^check_t)) {
      validators.push_back(
          std::meta::template_arguments_of(std::meta::type_of(annotation))[0]);
    }
  }
  return validators;
}

template <std::meta::info Entity>
inline constexpr auto validators_v = std::define_static_array(validators_of(Entity));

// The function types a validator of a `Value` may have.
template <typename Value> using validator_function = Status(const Value&);
template <typename Value>
using noexcept_validator_function = Status(const Value&) noexcept;

// Whether `function`, a function type, is that of a validator of a `type`.
consteval bool is_validator_of(const std::meta::info function,
                               const std::meta::info type) {
  // Parenthesized so clang-format, which does not know `^^`, lays them out.
  for (const auto alias : {(^^validator_function), (^^noexcept_validator_function)}) {
    if (function == std::meta::dealias(std::meta::substitute(alias, {type}))) {
      return true;
    }
  }
  return false;
}

template <typename Pointer> inline constexpr Pointer null_pointer_v = nullptr;

// Compares reflections rather than pointers: under -fsanitize=null, which
// -fsanitize=undefined enables, GCC 16 does not constant-fold the comparison of
// a function's address with nullptr.
consteval bool is_null_pointer(const std::meta::info pointer) {
  // Parenthesized so clang-format, which does not know `^^`, lays it out.
  const auto variable = (^^null_pointer_v);
  return pointer == std::meta::constant_of(
                        std::meta::substitute(variable, {std::meta::type_of(pointer)}));
}

// Why `validator`, a check annotation's argument, is not a validator of a value
// of `type`; empty when it is one.
consteval std::string validator_problem(const std::meta::info validator,
                                        const std::meta::info type) {
  const auto pointer = std::meta::type_of(validator);
  if (!std::meta::is_pointer_type(pointer) ||
      !std::meta::is_function_type(std::meta::remove_pointer(pointer))) {
    return "carries scrydantic::check<" + display(validator) +
           ">, whose argument is not a pointer to a function";
  }
  if (is_null_pointer(validator)) {
    return "carries a scrydantic::check of a null function pointer";
  }
  const auto function = std::meta::dealias(std::meta::remove_pointer(pointer));
  if (!is_validator_of(function, type)) {
    return "carries scrydantic::check<&" + display(validator) +
           ">, whose validator is " + display(function) + "; a validator here takes " +
           display(std::meta::add_lvalue_reference(std::meta::add_const(type))) +
           " and returns scrydantic::Status";
  }
  return {};
}

// Why the check annotations of `entity` do not each name a distinct validator of
// a value of `type`; empty when they do.
consteval std::string validators_problem(const std::meta::info entity,
                                         const std::meta::info type) {
  const auto validators = validators_of(entity);
  for (const auto validator : validators) {
    if (auto text = validator_problem(validator, type); !text.empty()) {
      return text;
    }
  }
  for (std::size_t left = 0; left < validators.size(); ++left) {
    for (std::size_t right = left + 1; right < validators.size(); ++right) {
      if (validators[left] == validators[right]) {
        return "carries scrydantic::check<&" + display(validators[left]) + "> twice";
      }
    }
  }
  return {};
}

} // namespace scrydantic::detail
