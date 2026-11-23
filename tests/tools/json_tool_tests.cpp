// Tools that take and return a Json: the parameter lists the empty schema and
// receives the canonical text of the argument, and a returned Json reaches the
// caller as the tool wrote it.

#include <catch2/catch_test_macros.hpp>
#include <cstddef>
#include <expected>
#include <optional>
#include <scrydantic/scrydantic.hpp>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace document_tools {

using scrydantic::Json;

struct Stored {
  Json document;
  std::size_t revision{};
};

// The text the tool received, so a test can read it back as a JSON string.
[[= scrydantic::tool{"Store a document"}]] inline std::string store(Json document) {
  return document.text;
}

[[= scrydantic::tool{"Return a document as it is"}]] inline Json
    forward(Json document) {
  return document;
}

[[= scrydantic::tool{"Return a stored document"}]] inline Json load() {
  return Json{.text = R"({ "title" : "Draft", "pages" : 2 })"};
}

[[= scrydantic::tool{"Wrap a document"}]] inline Stored wrap(Json document,
                                                             std::size_t revision) {
  return Stored{.document = std::move(document), .revision = revision};
}

[[= scrydantic::tool{"Find a document"}]] inline scrydantic::Result<Json>
    find(std::string id) {
  if (id.empty()) {
    return std::unexpected(scrydantic::tool_error("no document has an empty id"));
  }
  return Json{.text = R"({"id":")" + id + R"("})"};
}

[[= scrydantic::tool{"Tag a document"}]] inline std::string
    tag(std::optional<Json> label, std::vector<Json> parts) {
  return (label ? label->text : std::string{"none"}) + "/" +
         std::to_string(parts.size());
}

[[= scrydantic::tool{"Return text that is not JSON"}]] inline Json broken() {
  return Json{.text = "{"};
}

} // namespace document_tools

namespace {

[[nodiscard]] scrydantic::Json document_json(const std::string_view text) {
  return scrydantic::Json{.text = std::string{text}};
}

} // namespace

static_assert(scrydantic::tools_diagnostic<^^document_tools>().empty());

TEST_CASE("a tool with a Json parameter lists the empty schema for it") {
  const auto tools = scrydantic::bind<^^document_tools>();
  REQUIRE(tools.size() == 7);

  CHECK(
      tools[0].input_schema.text ==
      R"({"additionalProperties":false,"properties":{"document":{}},"required":["document"],"type":"object"})");
  CHECK(tools[3].input_schema.text ==
        R"({"additionalProperties":false,"properties":{"document":{},"revision":{)"
        R"("maximum":18446744073709551615,"minimum":0,"type":"integer"}},)"
        R"("required":["document","revision"],"type":"object"})");
  CHECK(tools[5].input_schema.text ==
        R"({"additionalProperties":false,"properties":{"label":{"anyOf":[{},)"
        R"({"type":"null"}]},"parts":{"items":{},"type":"array"}},)"
        R"("required":["label","parts"],"type":"object"})");
}

TEST_CASE("a Json parameter receives the canonical text of its argument") {
  const auto tools = scrydantic::bind<^^document_tools>();

  const auto stored =
      tools[0].invoke(document_json(R"({"document":{"b":[1, 2.50],"a":null}})"));
  REQUIRE(stored);
  CHECK(stored->text == R"("{\"a\":null,\"b\":[1,2.5]}")");

  // Any JSON value is a document, as the empty schema says.
  const auto scalar = tools[0].invoke(document_json(R"({"document":"plain"})"));
  REQUIRE(scalar);
  CHECK(scalar->text == R"("\"plain\"")");

  const auto missing = tools[0].invoke(document_json("{}"));
  REQUIRE_FALSE(missing);
  CHECK(missing.error().model_message == "$.document is a required member");

  const auto tagged =
      tools[5].invoke(document_json(R"({"label":null,"parts":[1,"two",{"n":3}]})"));
  REQUIRE(tagged);
  CHECK(tagged->text == R"("none/3")");
  const auto labelled = tools[5].invoke(document_json(R"({"label":[ 1 ],"parts":[]})"));
  REQUIRE(labelled);
  CHECK(labelled->text == R"("[1]/0")");
}

TEST_CASE("a tool returns a Json, or an aggregate holding one, as it wrote it") {
  const auto tools = scrydantic::bind<^^document_tools>();

  const auto forwarded =
      tools[1].invoke(document_json(R"({"document":{"z":1,"a":2}})"));
  REQUIRE(forwarded);
  CHECK(forwarded->text == R"({"a":2,"z":1})");

  // The tool's own text, whitespace and key order included.
  const auto loaded = tools[2].invoke(document_json("{}"));
  REQUIRE(loaded);
  CHECK(loaded->text == R"({ "title" : "Draft", "pages" : 2 })");

  const auto wrapped =
      tools[3].invoke(document_json(R"({"document":[true],"revision":4})"));
  REQUIRE(wrapped);
  CHECK(wrapped->text == R"({"document":[true],"revision":4})");

  const auto found = tools[4].invoke(document_json(R"({"id":"a1"})"));
  REQUIRE(found);
  CHECK(found->text == R"({"id":"a1"})");
  const auto refused = tools[4].invoke(document_json(R"({"id":""})"));
  REQUIRE_FALSE(refused);
  CHECK(refused.error().model_message == "no document has an empty id");
}

TEST_CASE("a returned Json that is not JSON is a host-side failure") {
  const auto tools = scrydantic::bind<^^document_tools>();

  const auto broken = tools[6].invoke(document_json("{}"));
  REQUIRE_FALSE(broken);
  CHECK(broken.error().message == "$ is not valid JSON");
  CHECK(broken.error().model_message.empty());
  CHECK(broken.error().issues.empty());
}

TEST_CASE("a typed handler takes and returns a Json") {
  const auto handler = scrydantic::bind_handler<document_tools::Stored>(
      [](document_tools::Stored stored) { return std::move(stored.document); });

  const auto document =
      handler(document_json(R"({"document":{"k": "v"},"revision":1})"));
  REQUIRE(document);
  CHECK(document->text == R"({"k":"v"})");
}
