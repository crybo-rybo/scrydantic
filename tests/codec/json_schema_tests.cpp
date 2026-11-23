// A Json is any JSON value: its schema is the empty schema in every position, it
// is a SupportedValue wherever it sits, and decoding and encoding treat it as
// they did before it had a schema.

#include <array>
#include <catch2/catch_test_macros.hpp>
#include <cstdint>
#include <memory>
#include <optional>
#include <scrydantic/annotations.hpp>
#include <scrydantic/codec.hpp>
#include <scrydantic/diagnostics.hpp>
#include <scrydantic/json.hpp>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace any_json {

using scrydantic::Json;

struct Payload {
  Json data;
};

// A description is the one keyword an empty schema carries; on an optional it
// sits beside the anyOf, as for any other optional member.
struct Described {
  [[= scrydantic::description{"Whatever the sender attached"}]] Json attachment;
  [[= scrydantic::description{"Free-form extras"}]] std::optional<Json> extras;
};

struct Envelope {
  std::array<Json, 2> pair;
  std::indirect<Json> boxed;
  std::vector<Json> items;
  [[= scrydantic::skip_null]] std::optional<Json> meta;
  std::string topic;
};

struct[[= scrydantic::tag{"raw"}]] Raw {
  Json value;
};

struct[[= scrydantic::tag{"text"}]] Text {
  std::string value;
};

using Block = std::variant<Text, Raw>;

// A recursive type that holds a payload: a definition whose property is `{}`.
struct Thread {
  Json body;
  std::vector<Thread> replies;
};

struct Forum {
  Thread pinned;
};

// A Json that is a template argument of a recursive type is part of its
// definition name, spelled as any other class is.
template <typename Value> struct Tree {
  std::vector<Tree> children;
  Value value;
};

struct Grove {
  Tree<Json> tree;
};

static_assert(scrydantic::SupportedValue<Json>);
static_assert(scrydantic::SupportedValue<const Json&>);
static_assert(scrydantic::Encodable<Json>);
static_assert(scrydantic::Decodable<Json>);
static_assert(scrydantic::SupportedValue<std::optional<Json>>);
static_assert(scrydantic::SupportedValue<std::vector<Json>>);
static_assert(scrydantic::SupportedValue<Payload>);
static_assert(scrydantic::SupportedValue<Envelope>);
static_assert(scrydantic::SupportedValue<Block>);
static_assert(scrydantic::SupportedValue<Forum>);
static_assert(scrydantic::supported_value_diagnostic<Json>().empty());
static_assert(scrydantic::supported_value_diagnostic<Payload>().empty());

// An aggregate that holds a Json is an argument object; a Json alone is not one,
// since it need not be a JSON object.
static_assert(scrydantic::ToolArguments<Payload>);
static_assert(!scrydantic::ToolArguments<Json>);

// A variant is still a tagged union of aggregates, so a Json is no alternative.
static_assert(!scrydantic::SupportedValue<std::variant<Text, Json>>);

static_assert(scrydantic::schema_v<Json> == "{}");
static_assert(scrydantic::schema_v<const Json&> == "{}");
static_assert(scrydantic::schema_v<std::indirect<Json>> == "{}");
static_assert(scrydantic::schema_v<std::optional<Json>> ==
              R"({"anyOf":[{},{"type":"null"}]})");
static_assert(scrydantic::schema_v<std::vector<Json>> ==
              R"({"items":{},"type":"array"})");
static_assert(scrydantic::schema_v<std::array<Json, 2>> ==
              R"({"items":{},"maxItems":2,"minItems":2,"type":"array"})");
static_assert(
    scrydantic::schema_v<Payload> ==
    R"({"additionalProperties":false,"properties":{"data":{}},"required":["data"],"type":"object"})");
static_assert(scrydantic::input_schema_v<Payload> == scrydantic::schema_v<Payload>);
static_assert(
    scrydantic::schema_v<Described> ==
    R"({"additionalProperties":false,"properties":{)"
    R"("attachment":{"description":"Whatever the sender attached"},)"
    R"("extras":{"anyOf":[{},{"type":"null"}],"description":"Free-form extras"}},)"
    R"("required":["attachment","extras"],"type":"object"})");
static_assert(
    scrydantic::schema_v<Envelope> ==
    R"({"additionalProperties":false,"properties":{"boxed":{},"items":{"items":{},)"
    R"("type":"array"},"meta":{"anyOf":[{},{"type":"null"}]},"pair":{"items":{},)"
    R"("maxItems":2,"minItems":2,"type":"array"},"topic":{"type":"string"}},)"
    R"("required":["boxed","items","pair","topic"],"type":"object"})");
static_assert(
    scrydantic::schema_v<Block> ==
    R"({"anyOf":[{"additionalProperties":false,"properties":{"type":{"enum":["text"],)"
    R"("type":"string"},"value":{"type":"string"}},"required":["type","value"],)"
    R"("type":"object"},{"additionalProperties":false,"properties":{"type":{)"
    R"("enum":["raw"],"type":"string"},"value":{}},"required":["type","value"],)"
    R"("type":"object"}]})");
static_assert(
    scrydantic::schema_v<Forum> ==
    R"({"$defs":{"any_json.Thread":{"additionalProperties":false,"properties":{)"
    R"("body":{},"replies":{"items":{"$ref":"#/$defs/any_json.Thread"},)"
    R"("type":"array"}},"required":["body","replies"],"type":"object"}},)"
    R"("additionalProperties":false,"properties":{"pinned":{)"
    R"("$ref":"#/$defs/any_json.Thread"}},"required":["pinned"],"type":"object"})");
static_assert(
    scrydantic::schema_v<Grove> ==
    R"({"$defs":{"any_json.Tree-scrydantic.Json":{"additionalProperties":false,)"
    R"("properties":{"children":{"items":{)"
    R"("$ref":"#/$defs/any_json.Tree-scrydantic.Json"},"type":"array"},"value":{}},)"
    R"("required":["children","value"],"type":"object"}},)"
    R"("additionalProperties":false,"properties":{"tree":{)"
    R"("$ref":"#/$defs/any_json.Tree-scrydantic.Json"}},"required":["tree"],)"
    R"("type":"object"})");

} // namespace any_json

TEST_CASE("a schema's Json positions are the empty schema, which is valid JSON") {
  for (const std::string_view schema :
       {scrydantic::schema_v<scrydantic::Json>,
        scrydantic::schema_v<std::optional<scrydantic::Json>>,
        scrydantic::schema_v<any_json::Described>,
        scrydantic::schema_v<any_json::Envelope>,
        scrydantic::schema_v<any_json::Forum>}) {
    const auto parsed = scrydantic::JsonView::parse(schema);
    REQUIRE(parsed);
    // A schema is written in canonical form, empty objects included.
    CHECK(parsed->to_json().text == schema);
  }
}

TEST_CASE("a Json with a schema still decodes to the canonical text of its value") {
  const auto payload =
      scrydantic::decode<any_json::Payload>(R"({"data":{"b":[1, 2.50],"a":null}})");
  REQUIRE(payload);
  CHECK(payload->data.text == R"({"a":null,"b":[1,2.5]})");

  // Every kind of value is accepted, as the empty schema says, null included.
  for (const std::string_view value : {"null", "true", "-3", R"("text")", "[]", "{}"}) {
    const auto decoded =
        scrydantic::decode<any_json::Payload>(R"({"data":)" + std::string{value} + "}");
    REQUIRE(decoded);
    CHECK(decoded->data.text == value);
  }

  // The member is required like any other without a default.
  const auto missing = scrydantic::decode<any_json::Payload>("{}");
  REQUIRE_FALSE(missing);
  CHECK(missing.error().model_message == "$.data is a required member");

  // An optional Json reads null as disengaged, so null never reaches the payload.
  const auto absent = scrydantic::decode<std::optional<scrydantic::Json>>("null");
  REQUIRE(absent);
  CHECK_FALSE(absent->has_value());
}

TEST_CASE("a Json with a schema is still checked and spliced when it is encoded") {
  const any_json::Envelope envelope{
      .pair = {scrydantic::Json{.text = "1"}, scrydantic::Json{.text = "[ 2 ]"}},
      .boxed = std::indirect<scrydantic::Json>(scrydantic::Json{.text = "null"}),
      .items = {scrydantic::Json{.text = R"({"z":1, "a":2})"}},
      .meta = std::nullopt,
      .topic = "news",
  };

  const auto canonical = scrydantic::encode(envelope);
  REQUIRE(canonical);
  CHECK(canonical->text ==
        R"({"boxed":null,"items":[{"a":2,"z":1}],"pair":[1,[2]],"topic":"news"})");

  const auto verbatim = scrydantic::encode_verbatim(envelope);
  REQUIRE(verbatim);
  CHECK(verbatim->text ==
        R"({"boxed":null,"items":[{"z":1, "a":2}],"pair":[1,[ 2 ]],"topic":"news"})");

  const auto decoded = scrydantic::decode<any_json::Envelope>(*canonical);
  REQUIRE(decoded);
  CHECK(decoded->items.front().text == R"({"a":2,"z":1})");
  CHECK_FALSE(decoded->meta.has_value());

  const auto invalid =
      scrydantic::encode(any_json::Payload{.data = scrydantic::Json{.text = "{"}});
  REQUIRE_FALSE(invalid);
  CHECK(invalid.error().message == "$.data is not valid JSON");
}
