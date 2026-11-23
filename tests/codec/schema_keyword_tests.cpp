// The annotations that add a keyword to a schema and do nothing else: where
// each keyword stands in the schema of a member and of a class, that encoding
// and decoding do not read them, and every one the compiler rejects, with its
// diagnostic.

#include <catch2/catch_test_macros.hpp>
#include <cstdint>
#include <memory>
#include <optional>
#include <scrydantic/annotations.hpp>
#include <scrydantic/codec.hpp>
#include <scrydantic/diagnostics.hpp>
#include <scrydantic/json.hpp>
#include <scrydantic/tools.hpp>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace {

using scrydantic::title;

// Index-based rather than string_view::find: under AddressSanitizer GCC 16 cannot
// constant-fold find's null check on an interior pointer of a static string.
[[nodiscard]] consteval bool mentions(const std::string_view text,
                                      const std::string_view fragment) {
  for (std::size_t start = 0; start + fragment.size() <= text.size(); ++start) {
    bool matched = true;
    for (std::size_t index = 0; index < fragment.size() && matched; ++index) {
      matched = text[start + index] == fragment[index];
    }
    if (matched) {
      return true;
    }
  }
  return false;
}

// ---- Titles on members ---------------------------------------------------------

enum class Tier { free, paid };

// A title joins the member's other keywords in sorted order, on whatever schema
// the member's type has.
struct Account {
  [[= title{"Display name"}]] std::string name;
  [[ = title{"Seats"}, = scrydantic::range{.min = 1} ]] std::int32_t seats;
  [[ = title{"Plan"}, = scrydantic::description{"What is paid for"} ]] Tier tier;
  [[ = title{"Labels"}, = scrydantic::items{.max = 3} ]] std::vector<std::string>
      labels;
  [[= title{"Is \"active\""}]] bool active;
  std::string untitled;
};

static_assert(
    scrydantic::schema_v<Account> ==
    R"({"additionalProperties":false,"properties":{)"
    R"("active":{"title":"Is \"active\"","type":"boolean"},)"
    R"("labels":{"items":{"type":"string"},"maxItems":3,"title":"Labels","type":"array"},)"
    R"("name":{"title":"Display name","type":"string"},)"
    R"("seats":{"maximum":2147483647,"minimum":1,"title":"Seats","type":"integer"},)"
    R"("tier":{"description":"What is paid for","enum":["free","paid"],)"
    R"("title":"Plan","type":"string"},)"
    R"("untitled":{"type":"string"}},)"
    R"("required":["active","labels","name","seats","tier","untitled"],)"
    R"("type":"object"})");

// On an optional member the title is the engaged value's, as a constraint is,
// while the description and the default stay the member's, beside the "anyOf".
// An indirect is the value it owns.
struct Contact {
  [[ = title{"Nickname"},
     = scrydantic::description{"If any"} ]] std::optional<std::string>
      nickname{};
  [[= title{"Extension"}]] std::optional<std::indirect<std::int16_t>> extension;
  [[= title{"Floor"}]] std::indirect<std::int16_t> floor;
};

static_assert(
    scrydantic::schema_v<Contact> ==
    R"({"additionalProperties":false,"properties":{)"
    R"("extension":{"anyOf":[{"maximum":32767,"minimum":-32768,)"
    R"("title":"Extension","type":"integer"},{"type":"null"}]},)"
    R"("floor":{"maximum":32767,"minimum":-32768,"title":"Floor","type":"integer"},)"
    R"("nickname":{"anyOf":[{"title":"Nickname","type":"string"},{"type":"null"}],)"
    R"("default":null,"description":"If any"}},)"
    R"("required":["extension","floor"],"type":"object"})");

// ---- Titles on classes ---------------------------------------------------------

struct[[= title{"Postal address"}]] Address {
  std::string city;
};

// A class's title stands on its object schema wherever the class is written,
// here in four places, and a member's own title replaces it on that member's
// schema.
struct Customer {
  Address home;
  Address work;
  std::optional<Address> holiday;
  std::vector<Address> previous;
  [[= title{"Billing address"}]] Address billing;
  [[= title{"Delivery address"}]] std::optional<Address> delivery;
};

static_assert(
    scrydantic::schema_v<Customer> ==
    R"({"additionalProperties":false,"properties":{)"
    R"("billing":{"additionalProperties":false,"properties":{"city":{"type":"string"}},)"
    R"("required":["city"],"title":"Billing address","type":"object"},)"
    R"("delivery":{"anyOf":[{"additionalProperties":false,)"
    R"("properties":{"city":{"type":"string"}},"required":["city"],)"
    R"("title":"Delivery address","type":"object"},{"type":"null"}]},)"
    R"("holiday":{"anyOf":[{"additionalProperties":false,)"
    R"("properties":{"city":{"type":"string"}},"required":["city"],)"
    R"("title":"Postal address","type":"object"},{"type":"null"}]},)"
    R"("home":{"additionalProperties":false,"properties":{"city":{"type":"string"}},)"
    R"("required":["city"],"title":"Postal address","type":"object"},)"
    R"("previous":{"items":{"additionalProperties":false,)"
    R"("properties":{"city":{"type":"string"}},"required":["city"],)"
    R"("title":"Postal address","type":"object"},"type":"array"},)"
    R"("work":{"additionalProperties":false,"properties":{"city":{"type":"string"}},)"
    R"("required":["city"],"title":"Postal address","type":"object"}},)"
    R"("required":["billing","delivery","holiday","home","previous","work"],)"
    R"("type":"object"})");

// The root's own title is one more of its keywords.
static_assert(
    scrydantic::schema_v<Address> ==
    R"({"additionalProperties":false,"properties":{"city":{"type":"string"}},)"
    R"("required":["city"],"title":"Postal address","type":"object"})");

// ---- Recursion -----------------------------------------------------------------

// A recursive root keeps its title, and a member that refers to it states its
// own title beside the "$ref", as it states its description there.
struct[[= title{"Comment"}]] Comment {
  std::string text;
  [[= title{"Replies"}]] std::vector<Comment> replies;
  [[ = title{"In reply to"},
     = scrydantic::description{"The parent"} ]] std::optional<std::indirect<Comment>>
      parent;
};

static_assert(
    scrydantic::schema_v<Comment> ==
    R"({"additionalProperties":false,"properties":{)"
    R"("parent":{"anyOf":[{"$ref":"#","title":"In reply to"},{"type":"null"}],)"
    R"("description":"The parent"},)"
    R"("replies":{"items":{"$ref":"#"},"title":"Replies","type":"array"},)"
    R"("text":{"type":"string"}},)"
    R"("required":["parent","replies","text"],"title":"Comment","type":"object"})");

// A recursive class that is not the root is written once, under "$defs", with
// its title; a reference to it carries only what the referring member states.
struct Thread {
  Comment first;
  [[= title{"Pinned comment"}]] Comment pinned;
};

static_assert(
    scrydantic::schema_v<Thread> ==
    R"({"$defs":{"anonymous.Comment":{"additionalProperties":false,"properties":{)"
    R"("parent":{"anyOf":[{"$ref":"#/$defs/anonymous.Comment","title":"In reply to"},)"
    R"({"type":"null"}],"description":"The parent"},)"
    R"("replies":{"items":{"$ref":"#/$defs/anonymous.Comment"},)"
    R"("title":"Replies","type":"array"},)"
    R"("text":{"type":"string"}},)"
    R"("required":["parent","replies","text"],"title":"Comment","type":"object"}},)"
    R"("additionalProperties":false,"properties":{)"
    R"("first":{"$ref":"#/$defs/anonymous.Comment"},)"
    R"("pinned":{"$ref":"#/$defs/anonymous.Comment","title":"Pinned comment"}},)"
    R"("required":["first","pinned"],"type":"object"})");

// ---- Variants ------------------------------------------------------------------

struct[[ = scrydantic::tag{"card"}, = title{"Card payment"} ]] Card {
  std::string number;
};

struct[[= scrydantic::tag{"cash"}]] Cash {};

// An alternative's class title stands on its object schema. A variant member's
// title stands beside the "anyOf", which is the value's schema; an optional
// variant has only the one "anyOf" that null joins, so its title is there too.
struct Checkout {
  [[= title{"Payment"}]] std::variant<Card, Cash> payment;
  [[= title{"Refund"}]] std::optional<std::variant<Card, Cash>> refund;
};

static_assert(
    scrydantic::schema_v<Checkout> ==
    R"({"additionalProperties":false,"properties":{)"
    R"("payment":{"anyOf":[{"additionalProperties":false,"properties":{)"
    R"("number":{"type":"string"},"type":{"enum":["card"],"type":"string"}},)"
    R"("required":["number","type"],"title":"Card payment","type":"object"},)"
    R"({"additionalProperties":false,"properties":{)"
    R"("type":{"enum":["cash"],"type":"string"}},"required":["type"],"type":"object"}],)"
    R"("title":"Payment"},)"
    R"("refund":{"anyOf":[{"additionalProperties":false,"properties":{)"
    R"("number":{"type":"string"},"type":{"enum":["card"],"type":"string"}},)"
    R"("required":["number","type"],"title":"Card payment","type":"object"},)"
    R"({"additionalProperties":false,"properties":{)"
    R"("type":{"enum":["cash"],"type":"string"}},"required":["type"],"type":"object"},)"
    R"({"type":"null"}],"title":"Refund"}},)"
    R"("required":["payment","refund"],"type":"object"})");

// ---- Tools ---------------------------------------------------------------------

namespace account_tools {

[[= scrydantic::tool{"Open an account"}]] std::string open(Account account) {
  return account.name;
}

} // namespace account_tools

// ---- Rejections ----------------------------------------------------------------

struct TwiceTitledMember {
  [[ = title{"One"}, = title{"Two"} ]] std::int32_t value{};
};

struct[[ = title{"One"}, = title{"Two"} ]] TwiceTitledClass {
  std::int32_t value{};
};

struct HoldsTwiceTitled {
  std::vector<TwiceTitledClass> values;
};

static_assert(mentions(scrydantic::supported_value_diagnostic<TwiceTitledMember>(),
                       "TwiceTitledMember::value: has more than one "
                       "scrydantic::title annotation"));
static_assert(mentions(scrydantic::supported_value_diagnostic<TwiceTitledClass>(),
                       "TwiceTitledClass has more than one scrydantic::title "
                       "annotation"));
static_assert(mentions(scrydantic::supported_value_diagnostic<HoldsTwiceTitled>(),
                       "HoldsTwiceTitled::values[]: {anonymous}::TwiceTitledClass has "
                       "more than one scrydantic::title annotation"));
// Encoding and decoding read no title, and reject a type whose schema could
// not state its titles all the same: what is written must mean something.
static_assert(mentions(scrydantic::encodable_diagnostic<TwiceTitledMember>(),
                       "has more than one scrydantic::title annotation"));
static_assert(mentions(scrydantic::decodable_diagnostic<TwiceTitledClass>(),
                       "has more than one scrydantic::title annotation"));

} // namespace

TEST_CASE("a title is in the schema and nowhere else") {
  const auto encoded =
      scrydantic::encode(Customer{.home = {.city = "Oslo"},
                                  .work = {.city = "Lima"},
                                  .holiday = std::nullopt,
                                  .previous = {},
                                  .billing = {.city = "Rome"},
                                  .delivery = Address{.city = "Bonn"}});
  REQUIRE(encoded);
  CHECK(encoded->text ==
        R"({"billing":{"city":"Rome"},"delivery":{"city":"Bonn"},"holiday":null,)"
        R"("home":{"city":"Oslo"},"previous":[],"work":{"city":"Lima"}})");

  const auto decoded = scrydantic::decode<Customer>(encoded->text);
  REQUIRE(decoded);
  CHECK(decoded->billing.city == "Rome");
  // A title is no key: the member is still read under its own.
  const auto titled =
      scrydantic::decode<Address>(R"({"Postal address":{"city":"Oslo"}})");
  REQUIRE_FALSE(titled);
}

TEST_CASE("a tool's argument aggregate carries its titles into the input schema") {
  const auto tools = scrydantic::bind<^^account_tools>();
  REQUIRE(tools.size() == 1);
  CHECK(tools[0].input_schema.text == scrydantic::schema_v<Account>);
  CHECK(tools[0].input_schema.text.contains(
      R"("name":{"title":"Display name","type":"string"})"));
}
