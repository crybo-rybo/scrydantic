// Annotations on the parameters of a bound function. A value parameter takes
// what a data member takes, and the member that the synthesized argument object
// gives it carries them, so the schema, decoding, and its diagnostics treat a
// parameter as they treat a member.

#include <algorithm>
#include <catch2/catch_test_macros.hpp>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <meta>
#include <optional>
#include <scrydantic/scrydantic.hpp>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {

struct Book {
  std::string title{};
};

// A host's call context. Scrydantic only passes it through.
struct Reader {
  std::string name{};
};

// Another library's annotation, which Scrydantic does not read.
struct column {
  int index{};
};

} // namespace

namespace checks {

inline scrydantic::Status even(const std::int32_t& value) {
  if (value % 2 != 0) {
    return std::unexpected(scrydantic::rejection("must be even", "odd copies"));
  }
  return {};
}

inline scrydantic::Status lowercase(const std::string& value) {
  if (std::ranges::any_of(
          value, [](const char letter) { return letter >= 'A' && letter <= 'Z'; })) {
    return std::unexpected(scrydantic::rejection("must be lowercase"));
  }
  return {};
}

} // namespace checks

namespace catalog {

[[= scrydantic::tool{"Search the catalog"}]] inline std::vector<Book>
    search([[= scrydantic::description{"Title or author"}]] std::string query,
           [[= scrydantic::range{.min = 1, .max = 50}]] std::int32_t limit) {
  return std::vector<Book>(static_cast<std::size_t>(limit), Book{.title = query});
}

// The annotations that name a member, bound a string, and bound a number.
[[= scrydantic::tool{"Find a shelf"}]] inline std::string
    shelf([[ = scrydantic::name{"shelf-id"},
             = scrydantic::title{"Shelf"} ]] std::string shelf,
          [[
            = scrydantic::length{.min = 2, .max = 8},
            = scrydantic::pattern{"^[a-z]+$"}
          ]] std::string aisle,
          [[= scrydantic::multiple_of{2}]] std::int32_t page_size) {
  return shelf + ":" + aisle + ":" + std::to_string(page_size);
}

// The annotations that bound a sequence, add a keyword, and let an optional
// parameter be left out.
[[= scrydantic::tool{"Browse the shelves"}]] inline std::string browse(
    [[
      = scrydantic::items{.max = 2},
      = scrydantic::keyword{"uniqueItems", "true"}
    ]] std::vector<std::string>
        genres,
    [[ = scrydantic::skip_null,
       = scrydantic::keyword_string{"format", "date"} ]] std::optional<std::string>
        since) {
  return std::to_string(genres.size()) + ":" + since.value_or("ever");
}

// Validators on parameters, one of them after a constraint, and an optional
// parameter that nothing lets the input leave out.
[[= scrydantic::tool{"Reserve copies"}]] inline std::string reserve(
    [[ = scrydantic::range{.min = 1},
       = scrydantic::check<&checks::even> ]] std::int32_t copies,
    [[= scrydantic::check<&checks::lowercase>]] std::string holder,
    std::optional<std::int32_t> floor) {
  return holder + ":" + std::to_string(copies) + ":" +
         std::to_string(floor.value_or(-1));
}

// A context is no argument; the values after it are annotated as any others.
[[= scrydantic::tool{"Lend a book"}]] inline std::string lend(
    const Reader& reader, [[
      = scrydantic::description{"Days until it is due"}, = scrydantic::range{.min = 1}
    ]] std::int32_t days) {
  return reader.name + ":" + std::to_string(days);
}

// The annotations are on the first declaration only; the definition repeats
// none of them, and a later declaration adds one more.
[[= scrydantic::tool{"Renew a loan"}]] std::int32_t
    renew([[= scrydantic::range{.max = 3}]] std::int32_t times);

inline std::int32_t renew(std::int32_t times) { return times; }

std::int32_t renew([[= scrydantic::description{"How many times"}]] std::int32_t times);

} // namespace catalog

// A function that is no tool, bound by hand with arguments_t and apply().
namespace ledger {

inline std::int32_t
charge([[ = scrydantic::name{"amount"},
          = scrydantic::range{.min = 0} ]] std::int32_t cents,
       [[= column{3}]] std::string memo) {
  return cents + static_cast<std::int32_t>(memo.size());
}

} // namespace ledger

namespace {

class Desk {
public:
  [[= scrydantic::tool{"Stamp a card"}]] std::string stamp([[
    = scrydantic::description{"Card number"},
    = scrydantic::range{.min = 1000, .max = 9999}
  ]] std::int32_t card) const {
    return branch_ + "#" + std::to_string(card);
  }

private:
  std::string branch_{"main"};
};

[[nodiscard]] scrydantic::Json json(const std::string_view text) {
  return scrydantic::Json{.text = std::string{text}};
}

[[nodiscard]] std::string
model_message(const scrydantic::Result<scrydantic::Json>& result) {
  REQUIRE_FALSE(result.has_value());
  return result.error().model_message;
}

// The annotation of type `Annotation` on the member of `Type` named `name`.
template <typename Type, typename Annotation>
consteval std::optional<Annotation> member_annotation(const std::string_view name) {
  for (const auto member : std::meta::nonstatic_data_members_of(
           ^^Type, std::meta::access_context::unchecked())) {
    if (std::meta::identifier_of(member) != name) {
      continue;
    }
    for (const auto annotation : std::meta::annotations_of(member)) {
      if (std::meta::remove_cv(std::meta::type_of(annotation)) == ^^Annotation) {
        return std::meta::extract<Annotation>(annotation);
      }
    }
  }
  return std::nullopt;
}

} // namespace

TEST_CASE("a parameter's description and range are in the schema and checked") {
  const auto tools = scrydantic::bind<^^catalog::search>();
  REQUIRE(tools.size() == 1);
  CHECK(tools[0].input_schema.text ==
        R"({"additionalProperties":false,"properties":{)"
        R"("limit":{"maximum":50,"minimum":1,"type":"integer"},)"
        R"("query":{"description":"Title or author","type":"string"}},)"
        R"("required":["limit","query"],"type":"object"})");
  CHECK(tools[0].invoke(json(R"({"query":"x","limit":2})"))->text ==
        R"([{"title":"x"},{"title":"x"}])");
  CHECK(model_message(tools[0].invoke(json(R"({"query":"x","limit":99})"))) ==
        "$.limit must be at most 50");
}

TEST_CASE("name, title, length, pattern, and multiple_of apply to a parameter") {
  const auto tools = scrydantic::bind<^^catalog::shelf>();
  REQUIRE(tools.size() == 1);
  const auto& shelf = tools[0].invoke;
  CHECK(
      tools[0].input_schema.text ==
      R"({"additionalProperties":false,"properties":{)"
      R"("aisle":{"maxLength":8,"minLength":2,"pattern":"^[a-z]+$","type":"string"},)"
      R"("page_size":{"maximum":2147483647,"minimum":-2147483648,"multipleOf":2,"type":"integer"},)"
      R"("shelf-id":{"title":"Shelf","type":"string"}},)"
      R"("required":["aisle","page_size","shelf-id"],"type":"object"})");
  CHECK(shelf(json(R"({"shelf-id":"s1","aisle":"north","page_size":4})"))->text ==
        R"("s1:north:4")");

  // name is the key the input must use: the identifier is no member.
  CHECK(model_message(shelf(json(R"({"shelf":"s1","aisle":"north","page_size":4})"))) ==
        "$ contains unknown member \"shelf\"\n$[\"shelf-id\"] is a required member");

  // A value past a constraint is reported in the member's words. A pattern is
  // the schema's alone, as it is on a member.
  CHECK(model_message(shelf(json(R"({"shelf-id":"s1","aisle":"N","page_size":3})"))) ==
        "$.aisle must have at least 2 characters\n"
        "$.page_size must be a multiple of 2");
  CHECK(shelf(json(R"({"shelf-id":"s1","aisle":"NORTH","page_size":4})"))->text ==
        R"("s1:NORTH:4")");
}

TEST_CASE("items, keyword, keyword_string, and skip_null apply to a parameter") {
  const auto tools = scrydantic::bind<^^catalog::browse>();
  REQUIRE(tools.size() == 1);
  const auto& browse = tools[0].invoke;
  CHECK(
      tools[0].input_schema.text ==
      R"({"additionalProperties":false,"properties":{)"
      R"("genres":{"items":{"type":"string"},"maxItems":2,"type":"array","uniqueItems":true},)"
      R"("since":{"anyOf":[{"format":"date","type":"string"},{"type":"null"}]}},)"
      R"("required":["genres"],"type":"object"})");

  // skip_null lets `since` be left out, and null says the same.
  CHECK(browse(json(R"({"genres":["a"]})"))->text == R"("1:ever")");
  CHECK(browse(json(R"({"genres":[],"since":null})"))->text == R"("0:ever")");
  CHECK(browse(json(R"({"genres":[],"since":"2020-01-01"})"))->text ==
        R"("0:2020-01-01")");
  CHECK(model_message(browse(json(R"({"genres":["a","b","c"]})"))) ==
        "$.genres must have at most 2 items");
}

TEST_CASE("a parameter's validator runs on a value that meets its constraints") {
  const auto tools = scrydantic::bind<^^catalog::reserve>();
  REQUIRE(tools.size() == 1);
  const auto& reserve = tools[0].invoke;
  CHECK(reserve(json(R"({"copies":4,"holder":"ada","floor":2})"))->text ==
        R"("ada:4:2")");

  // The model is told each validator's model_message, and the host its message.
  const auto rejected = reserve(json(R"({"copies":3,"holder":"Ada","floor":null})"));
  CHECK(model_message(rejected) == "$.copies must be even\n$.holder must be lowercase");
  CHECK(rejected.error().message == "$.copies odd copies\n$.holder must be lowercase");
  REQUIRE(rejected.error().issues.size() == 2);
  CHECK(rejected.error().issues[0].kind == scrydantic::IssueKind::failed_check);

  // A value past the range is reported as that, and the validator does not run.
  CHECK(model_message(reserve(json(R"({"copies":-1,"holder":"ada","floor":null})"))) ==
        "$.copies must be at least 1");

  // An optional parameter without skip_null or a default argument may be null,
  // and must be there.
  CHECK(model_message(reserve(json(R"({"copies":4,"holder":"ada"})"))) ==
        "$.floor is a required member");
}

TEST_CASE("annotated values follow a context parameter") {
  const auto tools = scrydantic::bind<^^catalog::lend, Reader>();
  REQUIRE(tools.size() == 1);
  CHECK(
      tools[0].input_schema.text ==
      R"({"additionalProperties":false,"properties":{)"
      R"("days":{"description":"Days until it is due","maximum":2147483647,"minimum":1,"type":"integer"}},)"
      R"("required":["days"],"type":"object"})");
  const Reader reader{.name = "ada"};
  CHECK(tools[0].invoke(reader, json(R"({"days":7})"))->text == R"("ada:7")");
  CHECK(model_message(tools[0].invoke(reader, json(R"({"days":0})"))) ==
        "$.days must be at least 1");
}

TEST_CASE("annotations on any declaration of a function reach its parameters") {
  const auto tools = scrydantic::bind<^^catalog::renew>();
  REQUIRE(tools.size() == 1);
  CHECK(
      tools[0].input_schema.text ==
      R"({"additionalProperties":false,"properties":{)"
      R"("times":{"description":"How many times","maximum":3,"minimum":-2147483648,"type":"integer"}},)"
      R"("required":["times"],"type":"object"})");
  CHECK(model_message(tools[0].invoke(json(R"({"times":4})"))) ==
        "$.times must be at most 3");
}

TEST_CASE("a toolbox member function's parameters are annotated the same way") {
  const auto tools = scrydantic::bind(std::make_shared<const Desk>());
  REQUIRE(tools.size() == 1);
  CHECK(
      tools[0].input_schema.text ==
      R"({"additionalProperties":false,"properties":{)"
      R"("card":{"description":"Card number","maximum":9999,"minimum":1000,"type":"integer"}},)"
      R"("required":["card"],"type":"object"})");
  CHECK(tools[0].invoke(json(R"({"card":1234})"))->text == R"("main#1234")");
  CHECK(model_message(tools[0].invoke(json(R"({"card":12})"))) ==
        "$.card must be at least 1000");
}

TEST_CASE("describe() states what bind() does of annotated parameters") {
  static constexpr auto described = scrydantic::describe<^^catalog, Reader>();
  const auto bound = scrydantic::bind<^^catalog, Reader>();
  REQUIRE(described.size() == 6);
  REQUIRE(bound.size() == described.size());
  for (std::size_t index = 0; index < bound.size(); ++index) {
    CHECK(described[index].name == bound[index].name);
    CHECK(described[index].input_schema == bound[index].input_schema.text);
  }

  static constexpr auto desk = scrydantic::describe<Desk>();
  REQUIRE(desk.size() == 1);
  CHECK(desk[0].input_schema ==
        scrydantic::bind(std::make_shared<Desk>())[0].input_schema.text);
}

TEST_CASE("the argument object of any function carries its parameters' annotations") {
  using Arguments = scrydantic::arguments_t<^^ledger::charge>;
  static constexpr std::string_view schema = scrydantic::input_schema_v<Arguments>;
  CHECK(schema == R"({"additionalProperties":false,"properties":{)"
                  R"("amount":{"maximum":2147483647,"minimum":0,"type":"integer"},)"
                  R"("memo":{"type":"string"}},)"
                  R"("required":["amount","memo"],"type":"object"})");

  // The member keeps the parameter's identifier; the key is the annotation's.
  auto decoded = scrydantic::decode<Arguments>(R"({"amount":40,"memo":"ab"})");
  REQUIRE(decoded.has_value());
  CHECK(decoded->cents == 40);
  CHECK(scrydantic::encode(*decoded)->text == R"({"amount":40,"memo":"ab"})");
  CHECK(scrydantic::apply<^^ledger::charge>(std::move(*decoded)) == 42);
  const auto negative = scrydantic::decode<Arguments>(R"({"amount":-1,"memo":""})");
  REQUIRE_FALSE(negative.has_value());
  CHECK(negative.error().model_message == "$.amount must be at least 0");

  // An annotation that is not Scrydantic's rides along, for its own reader.
  static constexpr auto carried = member_annotation<Arguments, column>("memo");
  REQUIRE(carried.has_value());
  CHECK(carried->index == 3);
}
