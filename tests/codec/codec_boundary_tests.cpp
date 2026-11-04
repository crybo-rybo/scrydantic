// The codec at the edges of each scalar: the exact integer bounds and signs, the
// floating-point range, fixed-array sizes, the values encoding rejects, and the
// canonical number spelling encode() writes.

#include <array>
#include <catch2/catch_test_macros.hpp>
#include <cstdint>
#include <limits>
#include <optional>
#include <scrydantic/codec.hpp>
#include <scrydantic/error.hpp>
#include <scrydantic/json.hpp>
#include <string>
#include <string_view>
#include <vector>

namespace {

enum class Unit {
  celsius,
  fahrenheit,
};

struct Reading {
  std::array<std::int32_t, 2> fixed{};
  float floating{};
  double ratio{};
  Unit unit{Unit::celsius};
  std::vector<std::uint32_t> values{};
};

// The failure text for `text` decoded as `Type`, or "accepted". A strict decode
// failure gives the host and the model the same text, so this checks that too.
template <typename Type>
[[nodiscard]] std::string rejection(const std::string_view text) {
  const auto decoded =
      scrydantic::decode<Type>(scrydantic::Json{.text = std::string{text}});
  if (decoded) {
    return "accepted";
  }
  CHECK(decoded.error().model_message == decoded.error().message);
  return decoded.error().message;
}

template <typename Type>
[[nodiscard]] std::optional<Type> decoded(const std::string_view text) {
  auto value = scrydantic::decode<Type>(scrydantic::Json{.text = std::string{text}});
  if (!value) {
    return std::nullopt;
  }
  return *value;
}

// The failure text for encoding `value`, or "accepted". Encoding describes the
// program's own value, so a failure carries nothing for a model.
template <typename Type> [[nodiscard]] std::string encode_rejection(const Type& value) {
  const auto encoded = scrydantic::encode(value);
  const auto verbatim = scrydantic::encode_verbatim(value);
  CHECK(encoded.has_value() == verbatim.has_value());
  if (encoded) {
    return "accepted";
  }
  CHECK(encoded.error().model_message.empty());
  CHECK(verbatim.error().message == encoded.error().message);
  return encoded.error().message;
}

} // namespace

TEST_CASE("signed integers decode exactly to their bounds") {
  CHECK(decoded<std::int16_t>("-32768") == std::int16_t{-32768});
  CHECK(decoded<std::int16_t>("32767") == std::int16_t{32767});
  CHECK(rejection<std::int16_t>("-32769") == "$ is outside the integer range");
  CHECK(rejection<std::int16_t>("32768") == "$ is outside the integer range");

  CHECK(decoded<std::int32_t>("-2147483648") ==
        std::numeric_limits<std::int32_t>::min());
  CHECK(decoded<std::int32_t>("2147483647") ==
        std::numeric_limits<std::int32_t>::max());
  CHECK(rejection<std::int32_t>("-2147483649") == "$ is outside the integer range");
  CHECK(rejection<std::int32_t>("2147483648") == "$ is outside the integer range");

  CHECK(decoded<std::int64_t>("-9223372036854775808") ==
        std::numeric_limits<std::int64_t>::min());
  CHECK(decoded<std::int64_t>("9223372036854775807") ==
        std::numeric_limits<std::int64_t>::max());
  CHECK(rejection<std::int64_t>("9223372036854775808") ==
        "$ is outside the integer range");
}

TEST_CASE("an integer is written without a fraction, an exponent, or a negative zero") {
  // An exponent makes a number a double even when its value is whole, and -0 is
  // the double -0.0, so none of these is an integer.
  for (const std::string_view text :
       {"1.0", "1e2", "1E+2", "-0", "-9223372036854775809"}) {
    INFO(text);
    CHECK(rejection<std::int16_t>(text) == "$ must be an integer");
    CHECK(rejection<std::uint16_t>(text) == "$ must be an integer");
  }
  CHECK(rejection<std::int32_t>(R"("1")") == "$ must be an integer");
  CHECK(rejection<std::uint16_t>("false") == "$ must be an integer");
}

TEST_CASE("unsigned integers decode exactly to their bounds and reject a sign") {
  CHECK(decoded<std::uint16_t>("0") == std::uint16_t{0});
  CHECK(decoded<std::uint16_t>("65535") == std::uint16_t{65535});
  CHECK(rejection<std::uint16_t>("65536") == "$ is outside the integer range");
  CHECK(rejection<std::uint16_t>("-1") == "$ is outside the integer range");

  CHECK(decoded<std::uint64_t>("18446744073709551615") ==
        std::numeric_limits<std::uint64_t>::max());
  // One more no longer fits any integer, so the parser reads it as a double.
  CHECK(rejection<std::uint64_t>("18446744073709551616") == "$ must be an integer");
}

TEST_CASE("an integer out of range names its element and member") {
  constexpr std::string_view prefix =
      R"({"fixed":[1,2],"floating":0,"ratio":0,"unit":"celsius",)";
  CHECK(rejection<Reading>(std::string{prefix} +
                           R"("values":[4294967295,4294967296]})") ==
        "$.values[1] is outside the integer range");
  CHECK(rejection<Reading>(std::string{prefix} + R"("values":[1.5]})") ==
        "$.values[0] must be an integer");
  CHECK(decoded<Reading>(std::string{prefix} + R"("values":[4294967295]})")->values ==
        std::vector<std::uint32_t>{4294967295U});
}

TEST_CASE("floating-point values decode from every numeric kind within range") {
  CHECK(decoded<float>("7") == 7.0F);
  CHECK(decoded<float>("-1") == -1.0F);
  CHECK(decoded<float>("1.25") == 1.25F);
  CHECK(decoded<double>("-2") == -2.0);
  CHECK(decoded<double>("18446744073709551615") == 18446744073709551615.0);
  CHECK(decoded<double>("1.7976931348623157e308") ==
        std::numeric_limits<double>::max());

  // Past a float's range, and so small that a float holds only zero.
  for (const std::string_view text : {"1e39", "-1e39", "1e-50", "-1e-50"}) {
    INFO(text);
    CHECK(rejection<float>(text) == "$ must be a finite in-range number");
  }
  CHECK(decoded<float>("0") == 0.0F);
  CHECK(rejection<float>(R"("1.0")") == "$ must be a number");
  CHECK(rejection<double>("null") == "$ must be a number");

  CHECK(rejection<Reading>(R"({"fixed":[1,2],"floating":1e39})") ==
        "$.floating must be a finite in-range number");
}

TEST_CASE("fixed arrays require their exact size before their elements") {
  using Fixed = std::array<std::int32_t, 2>;
  CHECK(decoded<Fixed>("[1,2]") == Fixed{1, 2});
  for (const std::string_view text : {"[]", "[1]", "[1,2,3]", "{}", R"("12")"}) {
    INFO(text);
    CHECK(rejection<Fixed>(text) == "$ must be an array of the declared fixed size");
  }
  CHECK(rejection<Fixed>(R"([1,"2"])") == "$[1] must be an integer");
  CHECK(rejection<Reading>(R"({"fixed":[1]})") ==
        "$.fixed must be an array of the declared fixed size");
}

TEST_CASE("encoding rejects a number that JSON cannot spell") {
  for (const double value : {std::numeric_limits<double>::infinity(),
                             -std::numeric_limits<double>::infinity(),
                             std::numeric_limits<double>::quiet_NaN()}) {
    CHECK(encode_rejection(value) == "$ must be finite");
    CHECK(encode_rejection(static_cast<float>(value)) == "$ must be finite");
    CHECK(encode_rejection(std::vector<double>{1.0, value}) == "$[1] must be finite");
    CHECK(encode_rejection(Reading{.ratio = value}) == "$.ratio must be finite");
  }
  CHECK(encode_rejection(Reading{.floating = std::numeric_limits<float>::max()}) ==
        "accepted");
}

TEST_CASE("encoding rejects an enumeration value with no enumerator") {
  const auto undeclared = static_cast<Unit>(99);
  CHECK(encode_rejection(undeclared) == "$ is not a declared enumerator value");
  CHECK(encode_rejection(std::optional<Unit>{undeclared}) ==
        "$ is not a declared enumerator value");
  CHECK(encode_rejection(Reading{.unit = undeclared}) ==
        "$.unit is not a declared enumerator value");
  CHECK(scrydantic::encode(Unit::fahrenheit)->text == R"("fahrenheit")");
}

TEST_CASE("encode() spells numbers canonically") {
  CHECK(scrydantic::encode(0.1)->text == "0.1");
  CHECK(scrydantic::encode(1.0)->text == "1");
  CHECK(scrydantic::encode(1e20)->text == "1E20");
  CHECK(scrydantic::encode(1e-7)->text == "1E-7");
  // Canonical text keeps negative zero's sign.
  CHECK(scrydantic::encode(-0.0)->text == "-0");
  // A float is spelled as the float it is, not as the double it widens to.
  CHECK(scrydantic::encode(0.7F)->text == "0.7");
  CHECK(scrydantic::encode(std::int16_t{-32768})->text == "-32768");
  CHECK(scrydantic::encode(std::numeric_limits<std::uint64_t>::max())->text ==
        "18446744073709551615");
  CHECK(scrydantic::encode(std::numeric_limits<std::int64_t>::min())->text ==
        "-9223372036854775808");
}
