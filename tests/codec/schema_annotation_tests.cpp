// Class and scoped-enum descriptions, plus first-class format and deprecated
// schema annotations across members, optionals, indirects, classes, enums,
// recursive definitions, and tool parameters.

#include <catch2/catch_test_macros.hpp>
#include <cstdint>
#include <memory>
#include <optional>
#include <scrydantic/scrydantic.hpp>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace {

constexpr std::string_view address_summary = "A postal address";

struct[[
  = scrydantic::description_of<address_summary>(),
  = scrydantic::title{"Postal address"}
]] Address {
  std::string city;
};

struct[[= scrydantic::description{"A node in a comment tree"}]] TreeNode {
  std::string body;
  std::vector<TreeNode> replies;
};

enum class[[
  = scrydantic::description{"Temperature unit"},
  = scrydantic::title{"Unit"}
]] Unit : std::uint8_t {
  celsius,
  fahrenheit,
};

enum class[[
  = scrydantic::deprecated,
  = scrydantic::keyword_string{"x-since", "0.1"}
]] LegacyMode : std::uint8_t {
  basic,
  verbose,
};

struct[[= scrydantic::deprecated]] LegacyHeader {
  [[ = scrydantic::format{"date-time"},
     = scrydantic::description{"Timestamp"} ]] std::string sent_at;
  [[= scrydantic::deprecated]] std::optional<std::string> token;
  [[= scrydantic::format{"uri"}]] std::optional<std::indirect<std::string>> homepage;
};

struct Order {
  Address shipping;
  [[= scrydantic::description{"Where to send the invoice"}]] Address billing;
  std::optional<Address> backup;
  Unit unit;
  [[= scrydantic::description{"Display unit"}]] Unit display_unit;
  LegacyMode mode;
};

struct[[= scrydantic::description{"Duplicate"}]][[= scrydantic::description{"Again"}]]
    DoubleDescribedClass {
  int x{};
};

enum class[[= scrydantic::description{"First"}]]
          [[= scrydantic::description{"Second"}]] DoubleDescribedEnum : std::uint8_t {
            a,
          };

struct DoubleFormat {
  [[ = scrydantic::format{"date"}, = scrydantic::format{"date-time"} ]] std::string ts;
};

struct DoubleDeprecated {
  [[ = scrydantic::deprecated, = scrydantic::deprecated ]] std::string old_field;
};

} // namespace

namespace format_tools {

[[= scrydantic::tool{"Schedule a reminder"}]] inline std::string remind(
    [[= scrydantic::format{"date-time"}]] std::string when,
    [[= scrydantic::deprecated]] std::optional<std::string> channel = std::nullopt) {
  return channel ? when + ":" + *channel : when;
}

} // namespace format_tools

namespace {

TEST_CASE("classes and scoped enums emit their own descriptions and metadata") {
  CHECK(scrydantic::schema_v<Address> ==
        R"({"additionalProperties":false,"description":"A postal address",)"
        R"("properties":{"city":{"type":"string"}},"required":["city"],)"
        R"("title":"Postal address","type":"object"})");

  CHECK(scrydantic::schema_v<Unit> ==
        R"({"description":"Temperature unit","enum":["celsius","fahrenheit"],)"
        R"("title":"Unit","type":"string"})");

  CHECK(scrydantic::schema_v<LegacyMode> ==
        R"({"deprecated":true,"enum":["basic","verbose"],"type":"string",)"
        R"("x-since":"0.1"})");

  CHECK(scrydantic::schema_v<TreeNode> ==
        R"({"additionalProperties":false,"description":"A node in a comment tree",)"
        R"("properties":{"body":{"type":"string"},)"
        R"("replies":{"items":{"$ref":"#"},"type":"array"}},)"
        R"("required":["body","replies"],"type":"object"})");
}

TEST_CASE("member descriptions override class and enum descriptions") {
  CHECK(scrydantic::schema_v<Order> ==
        R"({"additionalProperties":false,"properties":{)"
        R"("backup":{"anyOf":[)"
        R"({"additionalProperties":false,"description":"A postal address",)"
        R"("properties":{"city":{"type":"string"}},"required":["city"],)"
        R"("title":"Postal address","type":"object"},{"type":"null"}]},)"
        R"("billing":{"additionalProperties":false,)"
        R"("description":"Where to send the invoice",)"
        R"("properties":{"city":{"type":"string"}},"required":["city"],)"
        R"("title":"Postal address","type":"object"},)"
        R"("display_unit":{"description":"Display unit",)"
        R"("enum":["celsius","fahrenheit"],"title":"Unit","type":"string"},)"
        R"("mode":{"deprecated":true,"enum":["basic","verbose"],"type":"string",)"
        R"("x-since":"0.1"},)"
        R"("shipping":{"additionalProperties":false,"description":"A postal address",)"
        R"("properties":{"city":{"type":"string"}},"required":["city"],)"
        R"("title":"Postal address","type":"object"},)"
        R"("unit":{"description":"Temperature unit","enum":["celsius","fahrenheit"],)"
        R"("title":"Unit","type":"string"}},)"
        R"("required":["backup","billing","display_unit","mode","shipping","unit"],)"
        R"("type":"object"})");
}

TEST_CASE("format and deprecated emit schema keywords and do not restrict decoding") {
  CHECK(scrydantic::schema_v<LegacyHeader> ==
        R"({"additionalProperties":false,"deprecated":true,"properties":{)"
        R"("homepage":{"anyOf":[{"format":"uri","type":"string"},{"type":"null"}]},)"
        R"("sent_at":{"description":"Timestamp","format":"date-time","type":"string"},)"
        R"("token":{"anyOf":[{"deprecated":true,"type":"string"},{"type":"null"}]}},)"
        R"("required":["homepage","sent_at","token"],"type":"object"})");

  // Schema-only: non-date text and deprecated fields still decode and encode.
  const auto decoded = scrydantic::decode<LegacyHeader>(
      R"({"homepage":"not a uri","sent_at":"yesterday","token":"abc"})");
  REQUIRE(decoded);
  CHECK(decoded->sent_at == "yesterday");
  CHECK(decoded->token == "abc");
  REQUIRE(decoded->homepage);
  CHECK(**decoded->homepage == "not a uri");
}

TEST_CASE("format and deprecated work on tool parameters") {
  const auto tools = scrydantic::bind<^^format_tools>();
  REQUIRE(tools.size() == 1);
  CHECK(tools[0].input_schema.text ==
        R"({"additionalProperties":false,"properties":{)"
        R"("channel":{"anyOf":[{"deprecated":true,"type":"string"},{"type":"null"}]},)"
        R"("when":{"format":"date-time","type":"string"}},)"
        R"("required":["when"],"type":"object"})");

  const auto reply = tools[0].invoke(R"({"when":"2026-11-28T15:00:00Z"})");
  REQUIRE(reply);
  CHECK(reply->text == R"("2026-11-28T15:00:00Z")");
}

TEST_CASE("duplicate class/enum descriptions, format, and deprecated are diagnosed") {
  CHECK(scrydantic::supported_value_diagnostic<DoubleDescribedClass>().contains(
      "has more than one scrydantic::description annotation"));
  CHECK(scrydantic::supported_value_diagnostic<DoubleDescribedEnum>().contains(
      "has more than one scrydantic::description annotation"));
  CHECK(scrydantic::supported_value_diagnostic<DoubleFormat>().contains(
      "has more than one keyword annotation with the key \"format\""));
  CHECK(scrydantic::supported_value_diagnostic<DoubleDeprecated>().contains(
      "has more than one keyword annotation with the key \"deprecated\""));
}

} // namespace
