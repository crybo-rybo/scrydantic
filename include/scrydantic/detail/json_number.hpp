#pragma once

#include <algorithm>
#include <bit>
#include <compare>
#include <cstddef>
#include <cstdint>
#include <iterator>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

// The canonical spelling of numbers, in code that also runs at compile time, so
// that a generated schema writes a bound exactly as the JSON core's canonical
// writer spells the same number at run time. The core reads a double's shortest
// round-trip digits from std::to_chars, which is not constexpr for floating-point
// types, so these functions find the same digits from the exact decimal values of
// the double and of the two halfway points to its neighbours. They favour plain
// arithmetic over speed, since a schema spells each of its bounds once.
namespace scrydantic::detail {

// The decimal digits of `value`.
constexpr std::string decimal(std::uint64_t value) {
  std::string digits{};
  do {
    digits.push_back(static_cast<char>('0' + (value % 10U)));
    value /= 10U;
  } while (value != 0U);
  std::ranges::reverse(digits);
  return digits;
}

// A non-negative number, `digits` times ten to the `exponent`. The first digit
// is not zero, and zero has no digits.
struct exact_decimal {
  std::string digits{};
  int exponent{};
};

// A natural number in base 10^9, least significant limb first.
using decimal_limbs = std::vector<std::uint32_t>;

inline constexpr std::uint32_t decimal_limb_base = 1'000'000'000U;

constexpr void multiply(decimal_limbs& number, const std::uint32_t factor) {
  // A limb is below 10^9 and the factor below 2^32, so the product and the
  // carry fit in 64 bits.
  std::uint64_t carry = 0;
  for (auto& limb : number) {
    const std::uint64_t product = (std::uint64_t{limb} * factor) + carry;
    limb = static_cast<std::uint32_t>(product % decimal_limb_base);
    carry = product / decimal_limb_base;
  }
  for (; carry != 0; carry /= decimal_limb_base) {
    number.push_back(static_cast<std::uint32_t>(carry % decimal_limb_base));
  }
}

// The exact value of mantissa * 2^exponent. A negative power of two is the same
// power of five over a power of ten, so both directions only multiply.
constexpr exact_decimal dyadic_decimal(std::uint64_t mantissa, const int exponent) {
  if (mantissa == 0) {
    return {};
  }
  decimal_limbs number{};
  for (; mantissa != 0; mantissa /= decimal_limb_base) {
    number.push_back(static_cast<std::uint32_t>(mantissa % decimal_limb_base));
  }
  const bool fraction = exponent < 0;
  // 5^13 and 2^31 are the largest powers of each that fit in a factor.
  const int step = fraction ? 13 : 31;
  const std::uint32_t base = fraction ? 5U : 2U;
  for (int remaining = fraction ? -exponent : exponent; remaining > 0;
       remaining -= step) {
    std::uint32_t factor = 1;
    for (int power = 0; power < std::min(step, remaining); ++power) {
      factor *= base;
    }
    multiply(number, factor);
  }
  std::string digits = decimal(number.back());
  for (auto limb = number.rbegin() + 1; limb != number.rend(); ++limb) {
    const auto group = decimal(*limb);
    digits.append(9U - group.size(), '0');
    digits += group;
  }
  return exact_decimal{.digits = std::move(digits),
                       .exponent = fraction ? exponent : 0};
}

constexpr std::strong_ordering compare(const exact_decimal& left,
                                       const exact_decimal& right) {
  if (left.digits.empty() || right.digits.empty()) {
    return !left.digits.empty() <=> !right.digits.empty();
  }
  // With no leading zeros, the position past the first digit orders magnitudes.
  const auto left_top = std::ssize(left.digits) + left.exponent;
  const auto right_top = std::ssize(right.digits) + right.exponent;
  if (left_top != right_top) {
    return left_top <=> right_top;
  }
  const auto width = std::max(left.digits.size(), right.digits.size());
  for (std::size_t index = 0; index < width; ++index) {
    const char left_digit = index < left.digits.size() ? left.digits[index] : '0';
    const char right_digit = index < right.digits.size() ? right.digits[index] : '0';
    if (left_digit != right_digit) {
      return left_digit <=> right_digit;
    }
  }
  return std::strong_ordering::equal;
}

// The first `count` digits of `value`, which has more, rounded down or up.
constexpr exact_decimal rounded(const exact_decimal& value, const std::size_t count,
                                const bool up) {
  exact_decimal result{
      .digits = value.digits.substr(0, count),
      .exponent = value.exponent + static_cast<int>(value.digits.size() - count),
  };
  if (up) {
    auto digit = result.digits.rbegin();
    for (; digit != result.digits.rend() && *digit == '9'; ++digit) {
      *digit = '0';
    }
    if (digit == result.digits.rend()) {
      result.digits.insert(result.digits.begin(), '1');
    } else {
      ++*digit;
    }
  }
  return result;
}

// How the digits of `value` past the first `count`, of which there is at least
// one, compare with half a unit of the last digit kept.
constexpr std::strong_ordering remainder_against_half(const exact_decimal& value,
                                                      const std::size_t count) {
  const auto rest = std::string_view{value.digits}.substr(count);
  if (rest.front() != '5') {
    return rest.front() <=> '5';
  }
  return rest.find_first_not_of('0', 1) == std::string_view::npos
             ? std::strong_ordering::equal
             : std::strong_ordering::greater;
}

// The decimals that read back as one double: the open interval between the
// halfway points to its neighbours, closed when ties to even round to it.
struct round_trip_interval {
  exact_decimal lower{};
  exact_decimal upper{};
  bool closed{};

  [[nodiscard]] constexpr bool contains(const exact_decimal& value) const {
    const auto above = compare(value, lower);
    const auto below = compare(value, upper);
    return closed ? above >= 0 && below <= 0 : above > 0 && below < 0;
  }
};

// A positive finite double as mantissa * 2^exponent.
struct binary_value {
  std::uint64_t mantissa{};
  int exponent{};
  // Whether the double below is half as far away as the one above, as it is at a
  // power of two, except at the smallest normal, below which the spacing stays
  // the same.
  bool narrow_below{};
};

constexpr binary_value binary_value_of(const double magnitude) {
  const auto bits = std::bit_cast<std::uint64_t>(magnitude);
  const std::uint64_t fraction = bits & ((std::uint64_t{1} << 52U) - 1U);
  const auto biased = static_cast<int>(bits >> 52U);
  return binary_value{
      .mantissa = biased == 0 ? fraction : fraction | (std::uint64_t{1} << 52U),
      .exponent = (biased == 0 ? 1 : biased) - 1075,
      .narrow_below = fraction == 0 && biased > 1,
  };
}

// The exact value of a positive finite double.
constexpr exact_decimal exact_decimal_of(const double magnitude) {
  const auto binary = binary_value_of(magnitude);
  return dyadic_decimal(binary.mantissa, binary.exponent);
}

// The shortest decimal that reads back as `magnitude`, a positive finite double,
// and of those the nearest, a tie going to an even last digit: the digits that
// std::to_chars writes.
constexpr exact_decimal shortest_decimal(const double magnitude) {
  const auto [mantissa, exponent, narrow_below] = binary_value_of(magnitude);
  const round_trip_interval interval{
      .lower = narrow_below ? dyadic_decimal((4U * mantissa) - 1U, exponent - 2)
                            : dyadic_decimal((2U * mantissa) - 1U, exponent - 1),
      .upper = dyadic_decimal((2U * mantissa) + 1U, exponent - 1),
      .closed = mantissa % 2U == 0U,
  };
  const auto value = dyadic_decimal(mantissa, exponent);
  // Of all the decimals with `count` digits, the two around the value are the
  // nearest to it, so if neither reads back as it, none does.
  for (std::size_t count = 1; count < value.digits.size(); ++count) {
    const auto down = rounded(value, count, false);
    const auto up = rounded(value, count, true);
    const bool down_fits = interval.contains(down);
    const bool up_fits = interval.contains(up);
    if (down_fits && up_fits) {
      const auto half = remainder_against_half(value, count);
      const bool even = (down.digits.back() - '0') % 2 == 0;
      return half < 0 || (half == 0 && even) ? down : up;
    }
    if (down_fits || up_fits) {
      return down_fits ? down : up;
    }
  }
  return value;
}

// Appends a finite double as the JSON core's canonical writer spells it
// (append_canonical_json_number): the shortest round-trip digits, laid out
// positionally when the decimal exponent of the first digit is from -4 to 15
// (0.0001, 1, 1234567890123456.8) and otherwise as `1.5E300` or `1E-5`. Zero is
// `0` and negative zero `-0`.
constexpr void append_canonical_double(std::string& output, const double value) {
  constexpr std::uint64_t sign_bit = std::uint64_t{1} << 63U;
  const auto bits = std::bit_cast<std::uint64_t>(value);
  if ((bits & sign_bit) != 0U) {
    output.push_back('-');
  }
  if ((bits & ~sign_bit) == 0U) {
    output.push_back('0');
    return;
  }
  auto shortest = shortest_decimal(std::bit_cast<double>(bits & ~sign_bit));
  for (; shortest.digits.back() == '0'; ++shortest.exponent) {
    shortest.digits.pop_back();
  }
  const std::string_view digits = shortest.digits;
  const auto count = std::ssize(digits);
  const auto leading = shortest.exponent + count - 1;
  if (leading < -4 || leading > 15) {
    output.push_back(digits.front());
    if (count > 1) {
      output.push_back('.');
      output.append(digits.substr(1));
    }
    output.append(leading < 0 ? "E-" : "E");
    output.append(
        decimal(static_cast<std::uint64_t>(leading < 0 ? -leading : leading)));
  } else if (leading < 0) {
    output.append("0.");
    output.append(static_cast<std::size_t>(-leading - 1), '0');
    output.append(digits);
  } else if (count <= leading + 1) {
    output.append(digits);
    output.append(static_cast<std::size_t>(leading + 1 - count), '0');
  } else {
    const auto whole = static_cast<std::size_t>(leading + 1);
    output.append(digits.substr(0, whole));
    output.push_back('.');
    output.append(digits.substr(whole));
  }
}

} // namespace scrydantic::detail
