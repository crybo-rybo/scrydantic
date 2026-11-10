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
[[nodiscard]] decode_result<Type> read_value(const JsonView& view,
                                             const decode_cursor& here);

template <typename Integer>
[[nodiscard]] decode_result<Integer> read_integer(const JsonView& view,
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
  return here.fail(IssueKind::wrong_type, "must be an integer");
}

template <typename Float>
[[nodiscard]] decode_result<Float> read_float(const JsonView& view,
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
    return here.fail(IssueKind::wrong_type, "must be a number");
  }

  // The range is checked on the double the parser read, which holds every
  // Float's maximum exactly; a double past it does not convert to a Float.
  constexpr auto maximum = static_cast<double>(std::numeric_limits<Float>::max());
  if (!std::isfinite(value) || value < -maximum || value > maximum) {
    return here.fail(IssueKind::out_of_range, "must be a finite in-range number");
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
[[nodiscard]] decode_result<Enum> read_enum(const JsonView& view,
                                            const decode_cursor& here) {
  if (view.kind() != JsonKind::string) {
    return here.fail(IssueKind::wrong_type, "must be an enumerator name");
  }
  const auto text = *view.string();
  template for (constexpr std::meta::info enumerator : enumerators_v<Enum>) {
    if (text == std::meta::identifier_of(enumerator)) {
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
[[nodiscard]] inline std::partial_ordering compare_parsed(const JsonView& view,
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

// Checks `value` against one end of a range, `limit` as a Number. A
// floating-point member is checked first as the parser read the number, so that
// decoding rejects whatever the schema does, and then as converted, so that the
// host's value is within the end too. Conversion rounds monotonically to values
// of Number, of which the limit is one, so it can carry a number onto an
// exclusive end but never past an end.
template <typename Number>
[[nodiscard]] bool
meets_end(const Number value, const Number limit, const checked_end& end,
          [[maybe_unused]] const JsonView& view, const decode_cursor& here) {
  if constexpr (std::floating_point<Number>) {
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

template <std::meta::info Constraint, typename Number>
[[nodiscard]] bool meets_range(const Number value, const JsonView& view,
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
[[nodiscard]] inline bool is_parsed_multiple(const JsonView& view,
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
// read the number and then as converted, as meets_end explains.
template <std::meta::info Constraint, typename Number>
[[nodiscard]] bool meets_multiple(const Number value,
                                  [[maybe_unused]] const JsonView& view,
                                  const decode_cursor& here) {
  constexpr checked_multiple multiple = checked_multiple_of(Constraint, ^^Number);
  constexpr auto factor = bound_value<Number>(multiple.value);
  if constexpr (std::floating_point<Number>) {
    if (!is_parsed_multiple(view, multiple, static_cast<double>(factor))) {
      here.report(IssueKind::violated_constraint, multiple.reason);
      return false;
    }
    if (std::fmod(value, factor) != Number{0}) {
      here.report(IssueKind::violated_constraint, multiple.rounded_reason);
      return false;
    }
  } else if (value % factor != 0) {
    here.report(IssueKind::violated_constraint, multiple.reason);
    return false;
  }
  return true;
}

template <std::meta::info Constraint>
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

// `view` is the JSON value that `value` decoded from.
template <std::meta::info Constraint, typename Value>
[[nodiscard]] bool meets(const Value& value, const JsonView& view,
                         const decode_cursor& here) {
  if constexpr (annotation_matches(Constraint, ^^range)) {
    return meets_range<Constraint>(value, view, here);
  } else if constexpr (annotation_matches(Constraint, ^^multiple_of)) {
    return meets_multiple<Constraint>(value, view, here);
  } else if constexpr (annotation_matches(Constraint, ^^length)) {
    return meets_size<Constraint>(code_points(value), here);
  } else {
    return meets_size<Constraint>(value.size(), here);
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
// value they own; a decoded indirect always owns one.
template <std::meta::info Member, typename Value>
[[nodiscard]] bool accepts_member(const Value& value, const JsonView& view,
                                  const decode_cursor& here) {
  if constexpr (kind_of(^^Value) == value_kind::optional) {
    return !value.has_value() || accepts_member<Member>(*value, view, here);
  } else if constexpr (kind_of(^^Value) == value_kind::indirect) {
    return accepts_member<Member>(*value, view, here);
  } else {
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

template <typename Type>
[[nodiscard]] bool is_declared_key(const std::string_view key) {
  static constexpr auto keys = [] {
    std::array<std::string_view, fields_v<Type>.size()> sorted{};
    for (std::size_t index = 0; index < sorted.size(); ++index) {
      sorted[index] = fields_v<Type>[index].key_view();
    }
    return sorted;
  }();
  return std::ranges::binary_search(keys, key);
}

// Reports each member of `view`, an object, that Type does not declare, and
// returns false if there was one. Tagged is true when the object is a variant
// alternative, whose discriminator member the variant has already read.
template <typename Type, bool Tagged>
[[nodiscard]] bool report_unknown_keys(const JsonView& view,
                                       const decode_cursor& here) {
  bool known = true;
  // key_at cannot be empty here: the view is an object and index is in range.
  for (std::size_t index = 0; index < view.size() && !here.stopped(); ++index) {
    const auto key = *view.key_at(index);
    if ((!Tagged || key != tag_key) && !is_declared_key<Type>(key)) {
      std::string reason{"contains unknown member "};
      append_json_string(reason, key);
      here.report(IssueKind::unknown_member, reason);
      known = false;
    }
  }
  return known;
}

// Reads one declared member of `object` from `view`, an object, and returns
// false if the member failed, missed a constraint, failed a check, or is
// required and absent. A member that keeps its default is the program's own
// value, so it is not checked.
template <typename Type, std::meta::info Member>
[[nodiscard]] bool read_member(Type& object, const JsonView& view,
                               const decode_cursor& parent) {
  constexpr field_layout field = field_of(^^Type, Member);
  constexpr std::string_view key = field.key_view();
  using Value = std::remove_cv_t<typename[:std::meta::type_of(Member):]>;
  const decode_cursor here = parent.member(key);
  if (const auto found = view.find(key)) {
    auto decoded = read_value<Value>(*found, here);
    if (!decoded) {
      return false;
    }
    if constexpr (!checked_constraints_v<Member>.empty() ||
                  !validators_v<Member>.empty()) {
      if (!accepts_member<Member>(*decoded, *found, here)) {
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

// Reads `view` into `object` and returns false if it failed. The class's
// validators run last, and see only an object whose every member is valid; in a
// recursive model each level's run once the levels inside it have passed theirs.
template <typename Type, bool Tagged>
[[nodiscard]] bool read_object(Type& object, const JsonView& view,
                               const decode_cursor& here) {
  if (view.kind() != JsonKind::object) {
    here.report(IssueKind::wrong_type, "must be an object");
    return false;
  }
  bool valid = true;
  if constexpr (!ignores_unknown(^^Type)) {
    valid = report_unknown_keys<Type, Tagged>(view, here);
  }
  template for (constexpr std::meta::info member : declared_members_v<Type>) {
    if (here.stopped()) {
      return false;
    }
    if (!read_member<Type, member>(object, view, here)) {
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
[[nodiscard]] decode_result<Type> read_in_place(const JsonView& view,
                                                const decode_cursor& here) {
  Type value = Type();
  if (!read_object<Type, Tagged>(value, view, here)) {
    return decode_failed<Type>();
  }
  return value;
}

// A missing or unknown tag leaves no alternative to check the object against, so
// it is the variant's only issue. The variant is built from the alternative its
// tag selects rather than read in place, which would default-construct its
// first alternative only to replace it, so it builds its failures itself.
template <typename Variant>
[[nodiscard]] decode_result<Variant> read_variant(const JsonView& view,
                                                  const decode_cursor& here) {
  static constexpr std::string_view tags = tag_names(^^Variant);
  if (view.kind() != JsonKind::object) {
    here.report(IssueKind::wrong_type, "must be an object");
    return decode_failed<Variant>();
  }
  const decode_cursor tag = here.member(tag_key);
  const auto discriminator = view.find(tag_key);
  if (!discriminator.has_value()) {
    tag.report(IssueKind::missing_tag,
               std::string{"is a required member; must be one of: "} +
                   std::string{tags});
    return decode_failed<Variant>();
  }

  const auto selected = discriminator->string();
  template for (constexpr std::meta::info alternative : alternatives_v<Variant>) {
    using Alternative = typename[:alternative:];
    if (selected == *tag_of(alternative)) {
      return read_in_place<Alternative, true>(view, here)
          .transform([](Alternative&& value) {
            return Variant{std::in_place_type<Alternative>, std::move(value)};
          });
    }
  }
  tag.report(IssueKind::unknown_tag,
             std::string{"must be one of: "} + std::string{tags});
  return decode_failed<Variant>();
}

template <typename Optional>
[[nodiscard]] decode_result<Optional> read_optional(const JsonView& view,
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
[[nodiscard]] decode_result<Indirect> read_indirect(const JsonView& view,
                                                    const decode_cursor& here) {
  using Owned = typename Indirect::value_type;
  return read_value<Owned>(view, here).transform([](Owned&& value) {
    return Indirect{std::in_place, std::move(value)};
  });
}

template <typename Vector>
[[nodiscard]] decode_result<Vector> read_vector(const JsonView& view,
                                                const decode_cursor& here) {
  if (view.kind() != JsonKind::array) {
    return here.fail(IssueKind::wrong_type, "must be an array");
  }
  Vector values{};
  values.reserve(view.size());
  bool valid = true;
  for (std::size_t index = 0; index < view.size() && !here.stopped(); ++index) {
    if (auto decoded = read_value<typename Vector::value_type>(*view.at(index),
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
[[nodiscard]] decode_result<Array> read_array(const JsonView& view,
                                              const decode_cursor& here) {
  constexpr std::string_view reason = "must be an array of the declared fixed size";
  // Value-initialized for the reason read_in_place's value is.
  Array values = Array();
  if (view.kind() != JsonKind::array) {
    return here.fail(IssueKind::wrong_type, reason);
  }
  if (view.size() != values.size()) {
    return here.fail(IssueKind::wrong_size, reason);
  }
  bool valid = true;
  for (std::size_t index = 0; index < values.size() && !here.stopped(); ++index) {
    if (auto decoded = read_value<typename Array::value_type>(*view.at(index),
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

// Booleans, strings, and raw JSON: the leaves that need no numeric range.
template <typename Type>
[[nodiscard]] decode_result<Type> read_plain_leaf(const JsonView& view,
                                                  const decode_cursor& here) {
  if constexpr (std::same_as<Type, bool>) {
    if (view.kind() != JsonKind::boolean) {
      return here.fail(IssueKind::wrong_type, "must be a boolean");
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
decode_result<Type> read_value(const JsonView& view, const decode_cursor& here) {
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
  } else if constexpr (kind == value_kind::variant) {
    return read_variant<Type>(view, here);
  } else if constexpr (kind == value_kind::aggregate) {
    return read_in_place<Type, false>(view, here);
  } else {
    return read_plain_leaf<Type>(view, here);
  }
}

// Decodes a value checked against a family, reporting every issue it finds with
// a schema-derived model_message.
template <typename Type> [[nodiscard]] Result<Type> decode(const JsonView& view) {
  decode_report report{};
  return read_value<Type>(view, decode_cursor{report})
      .transform_error(
          [&report](decode_failure) { return decode_error(std::move(report)); });
}

// Composed from std::expected's monadic operations, which build their failure
// with std::unexpect: see decode_failed(). Text that is not JSON is read once
// more to describe it, which only a failure pays for.
template <typename Args>
[[nodiscard]] Result<Args> decode_arguments(const Json& input) {
  return JsonView::parse(std::string_view{input.text})
      .transform_error([&input](const Error&) {
        return invalid_json_error("tool arguments are not valid JSON",
                                  describe_invalid_json(input.text));
      })
      .and_then([](const JsonView& view) { return detail::decode<Args>(view); });
}

} // namespace scrydantic::detail
