// The compile-time spelling of numbers, which generated schemas write bounds
// with, against format_json_number(), the JSON core's canonical writer, which
// writes the same numbers at run time.

#include <bit>
#include <catch2/catch_test_macros.hpp>
#include <charconv>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <random>
#include <scrydantic/detail/json_number.hpp>
#include <scrydantic/json.hpp>
#include <string>
#include <vector>

namespace {

[[nodiscard]] constexpr std::string spelled(const double value) {
  std::string text{};
  scrydantic::detail::append_canonical_double(text, value);
  return text;
}

// The powers of two, which are where the spacing of doubles changes, and their
// neighbours; the powers of ten; and random bit patterns and decimals.
[[nodiscard]] std::vector<double> spelling_cases() {
  std::vector<double> values{0.0, -0.0, std::numeric_limits<double>::denorm_min(),
                             std::numeric_limits<double>::min(),
                             std::numeric_limits<double>::max()};
  for (int exponent = -1074; exponent < 1024; ++exponent) {
    const double power = std::ldexp(1.0, exponent);
    values.insert(values.end(),
                  {power, std::nextafter(power, 0.0),
                   std::nextafter(power, std::numeric_limits<double>::max())});
  }
  // from_chars rather than std::stod, which throws for a subnormal result.
  const auto parsed = [](const std::string& text) {
    double value{};
    std::from_chars(text.data(), text.data() + text.size(), value);
    return value;
  };
  for (int exponent = -323; exponent <= 308; ++exponent) {
    values.push_back(parsed("1e" + std::to_string(exponent)));
  }
  std::mt19937_64 generator{20261103};
  for (int sample = 0; sample < 4000; ++sample) {
    const auto value = std::bit_cast<double>(generator());
    if (std::isfinite(value)) {
      values.push_back(value);
    }
  }
  std::uniform_int_distribution<std::int64_t> mantissa{-99999999, 99999999};
  std::uniform_int_distribution<int> decimal_exponent{-30, 30};
  for (int sample = 0; sample < 4000; ++sample) {
    values.push_back(parsed(std::to_string(mantissa(generator)) + "e" +
                            std::to_string(decimal_exponent(generator))));
  }
  for (const auto value : std::vector<double>{values}) {
    values.push_back(-value);
  }
  return values;
}

} // namespace

static_assert(scrydantic::detail::decimal(0) == "0");
static_assert(scrydantic::detail::decimal(18446744073709551615U) ==
              "18446744073709551615");

static_assert(spelled(0.1) == "0.1");
static_assert(spelled(-273.15) == "-273.15");
static_assert(spelled(1e300) == "1E300");
static_assert(spelled(1e-5) == "1E-5");
static_assert(spelled(0.0001) == "0.0001");
static_assert(spelled(1e15) == "1000000000000000");
static_assert(spelled(1e16) == "1E16");
static_assert(spelled(1234567890123456.8) == "1234567890123456.8");
static_assert(spelled(5e-324) == "5E-324");
static_assert(spelled(std::numeric_limits<double>::max()) == "1.7976931348623157E308");
static_assert(spelled(0.0) == "0");
static_assert(spelled(-0.0) == "-0");

TEST_CASE("doubles are spelled at compile time as the canonical writer spells them") {
  std::size_t mismatches = 0;
  for (const double value : spelling_cases()) {
    const auto canonical = scrydantic::format_json_number(value);
    if (spelled(value) != canonical && ++mismatches <= 5) {
      UNSCOPED_INFO(canonical << " was spelled " << spelled(value));
    }
  }
  CHECK(mismatches == 0);
}
