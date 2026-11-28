// Calling tools with plain JSON text (std::string_view, string literals,
// std::string) and binding tools with DecodeOptions for callers that stringify
// scalars or nested JSON values.

#include <catch2/catch_test_macros.hpp>
#include <cstdint>
#include <memory>
#include <scrydantic/scrydantic.hpp>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

namespace {

struct Session {
  std::string user{};
};

struct Filter {
  std::string city;
  [[= scrydantic::range{.min = 1, .max = 10}]] std::int32_t limit{5};
};

} // namespace

namespace weather_tools {

[[= scrydantic::tool{"Forecast weather"}]] inline std::string
    forecast(std::string city,
             [[= scrydantic::range{.min = 1, .max = 14}]] std::int32_t days,
             bool metric = true) {
  return city + ":" + std::to_string(days) + ":" + (metric ? "C" : "F");
}

[[= scrydantic::tool{"Search stations"}]] inline std::string
    search(Filter filter, std::vector<std::int32_t> codes) {
  return filter.city + ":" + std::to_string(filter.limit) + ":" +
         std::to_string(codes.size());
}

} // namespace weather_tools

namespace contextual_tools {

[[= scrydantic::tool{"Greet a user"}]] inline std::string greet(const Session& session,
                                                                std::int32_t times) {
  return session.user + "x" + std::to_string(times);
}

} // namespace contextual_tools

namespace {

class Ledger {
public:
  [[= scrydantic::tool{"Record an entry"}]] std::int64_t record(std::int64_t amount,
                                                                bool credit) {
    balance_ += credit ? amount : -amount;
    return balance_;
  }

private:
  std::int64_t balance_{};
};

using scrydantic::DecodeOptions;
using scrydantic::Json;
using scrydantic::JsonView;
using scrydantic::Result;

template <typename Set, typename... Arguments>
concept invocable_by_name = requires(Set& set, Arguments&&... arguments) {
  set.invoke(std::string_view{}, std::forward<Arguments>(arguments)...);
};

// ToolFunction and ToolSet both accept std::string_view alongside Json and JsonView.
static_assert(std::is_invocable_r_v<Result<Json>, const scrydantic::ToolFunction<>&,
                                    std::string_view>);
static_assert(std::is_invocable_r_v<Result<Json>, const scrydantic::ToolFunction<>&,
                                    const char*>);
static_assert(std::is_invocable_r_v<Result<Json>, const scrydantic::ToolFunction<>&,
                                    const std::string&>);
static_assert(
    std::is_invocable_r_v<Result<Json>, const scrydantic::ToolFunction<Session>&,
                          const Session&, std::string_view>);
static_assert(
    !std::is_invocable_v<const scrydantic::ToolFunction<Session>&, std::string_view>);
static_assert(!std::is_invocable_v<const scrydantic::ToolFunction<>&, const Session&,
                                   std::string_view>);

static_assert(invocable_by_name<const scrydantic::ToolSet<>, std::string_view>);
static_assert(invocable_by_name<const scrydantic::ToolSet<>, const char*>);
static_assert(invocable_by_name<const scrydantic::ToolSet<Session>, const Session&,
                                std::string_view>);
static_assert(!invocable_by_name<const scrydantic::ToolSet<Session>, std::string_view>);

TEST_CASE("tools and tool sets accept plain string_view JSON text directly") {
  auto set = scrydantic::ToolSet<>::make(scrydantic::bind<^^weather_tools>());
  REQUIRE(set);

  const auto literal_call = set->invoke("forecast", R"({"city":"Oslo","days":3})");
  REQUIRE(literal_call);
  CHECK(literal_call->text == R"("Oslo:3:C")");

  const std::string owned{R"({"city":"Lima","days":2,"metric":false})"};
  const auto string_call = set->find("forecast")->invoke(owned);
  REQUIRE(string_call);
  CHECK(string_call->text == R"("Lima:2:F")");

  const auto invalid = set->invoke("forecast", R"({"city" "Oslo"})");
  REQUIRE_FALSE(invalid);
  REQUIRE(invalid.error().issues.size() == 1);
  CHECK(invalid.error().issues[0].kind == scrydantic::IssueKind::invalid_json);
  CHECK(invalid.error().model_message ==
        "tool arguments are not valid JSON: expected ':' at line 1, column 9");

  const auto missing = set->invoke("unknown", "{}");
  REQUIRE_FALSE(missing);
  CHECK(missing.error().model_message ==
        R"(unknown tool "unknown"; known tools: forecast, search)");
}

TEST_CASE("contextual tools and custom handlers accept string_view") {
  auto set = scrydantic::ToolSet<Session>::make(
      scrydantic::bind<^^contextual_tools, Session>());
  REQUIRE(set);

  const Session session{.user = "ada"};
  const auto reply = set->invoke("greet", session, R"({"times":4})");
  REQUIRE(reply);
  CHECK(reply->text == R"("adax4")");

  // A hand-written Json handler receives raw text unchanged from string_view.
  std::string seen{};
  const scrydantic::ToolFunction<> raw_handler =
      [&seen](Json arguments) -> Result<Json> {
    seen = arguments.text;
    return Json{.text = "true"};
  };
  const auto raw_reply = raw_handler("not json at all");
  REQUIRE(raw_reply);
  CHECK(raw_reply->text == "true");
  CHECK(seen == "not json at all");

  // A hand-written JsonView handler receives the parsed view from string_view.
  const scrydantic::ToolFunction<> view_handler =
      [](const JsonView& arguments) -> Result<Json> {
    return Json{.text = std::to_string(arguments.size())};
  };
  const auto view_reply = view_handler(R"({"a":1,"b":2})");
  REQUIRE(view_reply);
  CHECK(view_reply->text == "2");
  CHECK_FALSE(view_handler("{").has_value());
}

TEST_CASE("bind forwards DecodeOptions to synthesized and aggregate tools") {
  const auto strict = scrydantic::bind<^^weather_tools>();
  const auto lenient = scrydantic::bind<^^weather_tools>(
      DecodeOptions{.coerce_scalars = true, .coerce_structures = true});

  // Options change decoding tolerance, never the published schema.
  CHECK(strict[0].input_schema.text == lenient[0].input_schema.text);
  CHECK(strict[1].input_schema.text == lenient[1].input_schema.text);

  // Strict binding rejects stringified scalars and structures.
  CHECK_FALSE(
      strict[0].invoke(R"({"city":"Oslo","days":"5","metric":"false"})").has_value());
  CHECK_FALSE(strict[1]
                  .invoke(R"({"filter":"{\"city\":\"Oslo\",\"limit\":\"3\"}",)"
                          R"("codes":"[1,2]"})")
                  .has_value());

  // Lenient binding decodes both and still checks constraints.
  const auto scalar_ok =
      lenient[0].invoke(R"({"city":"Oslo","days":"5","metric":"false"})");
  REQUIRE(scalar_ok);
  CHECK(scalar_ok->text == R"("Oslo:5:F")");

  const auto scalar_out_of_range = lenient[0].invoke(R"({"city":"Oslo","days":"99"})");
  REQUIRE_FALSE(scalar_out_of_range);
  CHECK(scalar_out_of_range.error().model_message == "$.days must be at most 14");

  const auto struct_ok = lenient[1].invoke(
      R"({"filter":"{\"city\":\"Oslo\",\"limit\":\"3\"}","codes":"[1,2]"})");
  REQUIRE(struct_ok);
  CHECK(struct_ok->text == R"("Oslo:3:2")");

  const auto struct_bad_limit = lenient[1].invoke(
      R"({"filter":"{\"city\":\"Oslo\",\"limit\":\"99\"}","codes":"[1,2]"})");
  REQUIRE_FALSE(struct_bad_limit);
  CHECK(struct_bad_limit.error().model_message == "$.filter.limit must be at most 10");

  // Single-function binding and contextual binding accept DecodeOptions too.
  const auto one = scrydantic::bind<^^weather_tools::forecast>(
      DecodeOptions{.coerce_scalars = true});
  const auto one_ok = one[0].invoke(R"({"city":"Oslo","days":"2"})");
  REQUIRE(one_ok);
  CHECK(one_ok->text == R"("Oslo:2:C")");

  const auto ctx = scrydantic::bind<^^contextual_tools, Session>(
      DecodeOptions{.coerce_scalars = true});
  const auto ctx_ok = ctx[0].invoke(Session{.user = "ada"}, R"({"times":"3"})");
  REQUIRE(ctx_ok);
  CHECK(ctx_ok->text == R"("adax3")");
}

TEST_CASE("toolbox bind and bind_handler accept DecodeOptions") {
  auto ledger = std::make_shared<Ledger>();
  const auto tools = scrydantic::bind(ledger, DecodeOptions{.coerce_scalars = true});
  REQUIRE(tools.size() == 1);

  const auto recorded = tools[0].invoke(R"({"amount":"25","credit":"true"})");
  REQUIRE(recorded);
  CHECK(recorded->text == "25");

  const auto handler = scrydantic::bind_handler<Filter>(
      [](Filter filter) { return filter.city + ":" + std::to_string(filter.limit); },
      DecodeOptions{.coerce_scalars = true});
  const auto handled = handler(R"({"city":"Bergen","limit":"7"})");
  REQUIRE(handled);
  CHECK(handled->text == R"("Bergen:7")");

  const auto view = JsonView::parse(R"({"city":"Bergen","limit":"8"})");
  REQUIRE(view);
  const auto from_view = handler(*view);
  REQUIRE(from_view);
  CHECK(from_view->text == R"("Bergen:8")");
}

} // namespace
