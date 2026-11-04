#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <limits>
#include <meta>
#include <optional>
#include <scrydantic/detail/failure.hpp>
#include <scrydantic/detail/json_string.hpp>
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
// A value fails when it, or any value inside it, does.
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
  long double value = 0.0L;
  switch (view.kind()) {
  case JsonKind::signed_integer:
    value = static_cast<long double>(*view.signed_integer());
    break;
  case JsonKind::unsigned_integer:
    value = static_cast<long double>(*view.unsigned_integer());
    break;
  case JsonKind::number:
    value = static_cast<long double>(*view.number());
    break;
  default:
    return here.fail(IssueKind::wrong_type, "must be a number");
  }

  const auto maximum = static_cast<long double>(std::numeric_limits<Float>::max());
  if (!std::isfinite(value) || value < -maximum || value > maximum) {
    return here.fail(IssueKind::out_of_range, "must be a finite in-range number");
  }
  const auto converted = static_cast<Float>(value);
  if (!std::isfinite(converted) || (value != 0.0L && converted == Float{0})) {
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
// false if the member failed or is required and absent.
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
    object.[:Member:] = std::move(*decoded);
  } else if constexpr (field.omit_null) {
    object.[:Member:].reset();
  } else if constexpr (field.required) {
    here.report(IssueKind::missing_member, "is a required member");
    return false;
  }
  return true;
}

template <typename Type, bool Tagged>
[[nodiscard]] decode_result<Type> read_object(const JsonView& view,
                                              const decode_cursor& here) {
  if (view.kind() != JsonKind::object) {
    return here.fail(IssueKind::wrong_type, "must be an object");
  }
  bool valid = true;
  if constexpr (!ignores_unknown(^^Type)) {
    valid = report_unknown_keys<Type, Tagged>(view, here);
  }

  Type object{};
  template for (constexpr std::meta::info member : declared_members_v<Type>) {
    if (here.stopped()) {
      return std::unexpected(decode_failure::reported);
    }
    if (!read_member<Type, member>(object, view, here)) {
      valid = false;
    }
  }
  if (!valid) {
    return std::unexpected(decode_failure::reported);
  }
  return object;
}

// A missing or unknown tag leaves no alternative to check the object against, so
// it is the variant's only issue.
template <typename Variant>
[[nodiscard]] decode_result<Variant> read_variant(const JsonView& view,
                                                  const decode_cursor& here) {
  static constexpr std::string_view tags = tag_names(^^Variant);
  if (view.kind() != JsonKind::object) {
    return here.fail(IssueKind::wrong_type, "must be an object");
  }
  const decode_cursor tag = here.member(tag_key);
  const auto discriminator = view.find(tag_key);
  if (!discriminator.has_value()) {
    return tag.fail(IssueKind::missing_tag,
                    std::string{"is a required member; must be one of: "} +
                        std::string{tags});
  }

  const auto selected = discriminator->string();
  template for (constexpr std::meta::info alternative : alternatives_v<Variant>) {
    using Alternative = typename[:alternative:];
    if (selected == *tag_of(alternative)) {
      return read_object<Alternative, true>(view, here)
          .transform([](Alternative&& value) {
            return Variant{std::in_place_type<Alternative>, std::move(value)};
          });
    }
  }
  return tag.fail(IssueKind::unknown_tag,
                  std::string{"must be one of: "} + std::string{tags});
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
  Array values{};
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
  } else if constexpr (kind == value_kind::vector) {
    return read_vector<Type>(view, here);
  } else if constexpr (kind == value_kind::array) {
    return read_array<Type>(view, here);
  } else if constexpr (kind == value_kind::variant) {
    return read_variant<Type>(view, here);
  } else if constexpr (kind == value_kind::aggregate) {
    return read_object<Type, false>(view, here);
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

template <ToolArguments Args>
[[nodiscard]] Result<Args> decode_arguments(const Json& input) {
  auto parsed = JsonView::parse(input);
  if (!parsed) {
    return std::unexpected(invalid_json_error("tool arguments are not valid JSON"));
  }
  return detail::decode<Args>(*parsed);
}

} // namespace scrydantic::detail
