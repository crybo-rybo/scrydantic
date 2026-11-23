// The annotations that add a keyword to a schema and do nothing else: where
// each keyword stands in the schema of a member and of a class, that encoding
// and decoding do not read them, and every one the compiler rejects, with its
// diagnostic.

#include <array>
#include <catch2/catch_test_macros.hpp>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <scrydantic/annotations.hpp>
#include <scrydantic/codec.hpp>
#include <scrydantic/detail/keywords.hpp>
#include <scrydantic/diagnostics.hpp>
#include <scrydantic/json.hpp>
#include <scrydantic/tools.hpp>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace {

using scrydantic::keyword;
using scrydantic::keyword_string;
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

// ---- Custom keywords -----------------------------------------------------------

// The issue's example: each keyword joins the member's schema with its value as
// written, in sorted order among the others.
struct Query {
  [[= keyword{"format", R"("date-time")"}]] std::string since;
  [[= keyword{"x-header", R"("Region")"}]] std::string region;
  [[= keyword{"examples", R"(["us-west1","eu-north1"])"}]] std::string zone;
};

static_assert(scrydantic::schema_v<Query> ==
              R"({"additionalProperties":false,"properties":{)"
              R"("region":{"type":"string","x-header":"Region"},)"
              R"("since":{"format":"date-time","type":"string"},)"
              R"("zone":{"examples":["us-west1","eu-north1"],"type":"string"}},)"
              R"("required":["region","since","zone"],"type":"object"})");

// A library's own annotation, which takes a plain name and spells no JSON.
namespace http {

template <std::size_t Size> consteval auto header(const char (&name)[Size]) {
  return scrydantic::keyword_string{"x-header", name};
}

} // namespace http

struct Routed {
  [[= http::header("Region")]] std::string region;
  [[= http::header("Tenant \"id\"")]] std::string tenant;
  [[= keyword_string{"format", "date-time"}]] std::string since;
};

static_assert(scrydantic::schema_v<Routed> ==
              R"({"additionalProperties":false,"properties":{)"
              R"("region":{"type":"string","x-header":"Region"},)"
              R"("since":{"format":"date-time","type":"string"},)"
              R"("tenant":{"type":"string","x-header":"Tenant \"id\""}},)"
              R"("required":["region","since","tenant"],"type":"object"})");

// The same schema whichever of the pair spells a string.
struct RoutedAsJson {
  [[= keyword{"x-header", R"("Region")"}]] std::string region;
  [[= keyword{"x-header", R"("Tenant \"id\"")"}]] std::string tenant;
  [[= keyword{"format", R"("date-time")"}]] std::string since;
};

static_assert(scrydantic::schema_v<RoutedAsJson> == scrydantic::schema_v<Routed>);

// Several keywords on one member stand in sorted order among its description,
// its constraints, its title, and the keywords of its type, wherever each sorts:
// "$comment" before them all and "x-unit" after. A value is any JSON value. It
// loses the whitespace between its tokens and is otherwise as it was written:
// the keys of an object in it are not sorted, and 2.50 stays 2.50.
struct Gauge {
  [[
    = keyword{"x-unit", R"("kPa")"},
    = keyword{"examples", " [ 1 ,\t{ \"b\" : 2.50 , \"a\" : [ ] } ,\n null ] "},
    = scrydantic::description{"Pressure"}, = keyword{"deprecated", "true"},
    = scrydantic::range{.min = 0}, = title{"Gauge pressure"},
    = keyword{"$comment", R"("a b")"}, = keyword{"readOnly", "false"},
    = keyword{"x-scale", "1e3"}
  ]] std::int32_t pressure;
};

static_assert(scrydantic::schema_v<Gauge> ==
              R"({"additionalProperties":false,"properties":{"pressure":{)"
              R"("$comment":"a b","deprecated":true,"description":"Pressure",)"
              R"("examples":[1,{"b":2.50,"a":[]},null],)"
              R"("maximum":2147483647,"minimum":0,"readOnly":false,)"
              R"("title":"Gauge pressure","type":"integer","x-scale":1e3,)"
              R"("x-unit":"kPa"}},"required":["pressure"],"type":"object"})");

// On an optional member a keyword joins the engaged value's schema, as a
// constraint does, and through an indirect that of the value it owns; the
// description and the default stay beside the "anyOf".
struct Window {
  [[
    = keyword_string{"format", "date"}, = scrydantic::length{.min = 10},
    = scrydantic::description{"First day"}
  ]] std::optional<std::string>
      from{};
  [[= keyword_string{"format", "date"}]] std::optional<std::indirect<std::string>> to;
  [[= keyword_string{"format", "time"}]] std::indirect<std::string> at;
};

static_assert(scrydantic::schema_v<Window> ==
              R"({"additionalProperties":false,"properties":{)"
              R"("at":{"format":"time","type":"string"},)"
              R"("from":{"anyOf":[{"format":"date","minLength":10,"type":"string"},)"
              R"({"type":"null"}],"default":null,"description":"First day"},)"
              R"("to":{"anyOf":[{"format":"date","type":"string"},{"type":"null"}]}},)"
              R"("required":["at","to"],"type":"object"})");

// On a class a keyword joins the object schema wherever the class is written. A
// member of that class's type that carries the same key shows its own value
// there, and adds what other keys it has.
struct[[ = keyword{"x-kind", R"("point")"}, = keyword{"$comment", R"("In metres")"} ]]
    Position {
  [[= keyword{"x-axis", "0"}]] double x;
};

struct Segment {
  Position from;
  [[ = keyword{"x-kind", R"("end")"}, = keyword{"x-last", "true"} ]] Position to;
  std::vector<Position> via;
};

static_assert(scrydantic::schema_v<Segment> ==
              R"({"additionalProperties":false,"properties":{)"
              R"("from":{"$comment":"In metres","additionalProperties":false,)"
              R"("properties":{"x":{"type":"number","x-axis":0}},"required":["x"],)"
              R"("type":"object","x-kind":"point"},)"
              R"("to":{"$comment":"In metres","additionalProperties":false,)"
              R"("properties":{"x":{"type":"number","x-axis":0}},"required":["x"],)"
              R"("type":"object","x-kind":"end","x-last":true},)"
              R"("via":{"items":{"$comment":"In metres","additionalProperties":false,)"
              R"("properties":{"x":{"type":"number","x-axis":0}},"required":["x"],)"
              R"("type":"object","x-kind":"point"},"type":"array"}},)"
              R"("required":["from","to","via"],"type":"object"})");

// A recursive class: its keywords are on the root, or on its entry under
// "$defs", and a keyword of a member that refers to it stands beside the "$ref",
// where its description stands. "$comment" sorts before "$defs" and "$ref".
struct[[ = keyword{"x-kind", R"("folder")"}, = keyword{"$comment", R"("A tree")"} ]]
    Folder {
  [[= keyword{"x-order", R"("by name")"}]] std::vector<Folder> folders;
  [[ = keyword{"$comment", R"("Up")"},
     = keyword{"x-link", "true"} ]] std::optional<std::indirect<Folder>>
      parent;
};

static_assert(
    scrydantic::schema_v<Folder> ==
    R"({"$comment":"A tree","additionalProperties":false,"properties":{)"
    R"("folders":{"items":{"$ref":"#"},"type":"array","x-order":"by name"},)"
    R"("parent":{"anyOf":[{"$comment":"Up","$ref":"#","x-link":true},{"type":"null"}]}},)"
    R"("required":["folders","parent"],"type":"object","x-kind":"folder"})");

struct Drive {
  [[= keyword{"x-kind", R"("root")"}]] Folder root;
  Folder trash;
};

static_assert(
    scrydantic::schema_v<Drive> ==
    R"({"$defs":{"anonymous.Folder":{"$comment":"A tree","additionalProperties":false,)"
    R"("properties":{"folders":{"items":{"$ref":"#/$defs/anonymous.Folder"},)"
    R"("type":"array","x-order":"by name"},)"
    R"("parent":{"anyOf":[{"$comment":"Up","$ref":"#/$defs/anonymous.Folder",)"
    R"("x-link":true},{"type":"null"}]}},)"
    R"("required":["folders","parent"],"type":"object","x-kind":"folder"}},)"
    R"("additionalProperties":false,"properties":{)"
    R"("root":{"$ref":"#/$defs/anonymous.Folder","x-kind":"root"},)"
    R"("trash":{"$ref":"#/$defs/anonymous.Folder"}},)"
    R"("required":["root","trash"],"type":"object"})");

// A root that is not an object and has definitions: a keyword sorts around
// "$defs" as around any other.
static_assert(scrydantic::schema_v<std::vector<Folder>>.starts_with(
    R"({"$defs":{"anonymous.Folder":{"$comment":"A tree",)"));

// A variant alternative's class keywords are on its object schema, and a
// variant member's beside the "anyOf".
struct[[ = scrydantic::tag{"wire"}, = keyword{"x-fee", "25"} ]] Wire {};

struct Transfer {
  [[= keyword{"x-audited", "true"}]] std::variant<Wire, Cash> by;
  [[= keyword{"x-audited", "false"}]] std::optional<std::variant<Wire, Cash>> back;
};

static_assert(
    scrydantic::schema_v<Transfer> ==
    R"({"additionalProperties":false,"properties":{)"
    R"("back":{"anyOf":[{"additionalProperties":false,"properties":{)"
    R"("type":{"enum":["wire"],"type":"string"}},"required":["type"],)"
    R"("type":"object","x-fee":25},{"additionalProperties":false,"properties":{)"
    R"("type":{"enum":["cash"],"type":"string"}},"required":["type"],"type":"object"},)"
    R"({"type":"null"}],"x-audited":false},)"
    R"("by":{"anyOf":[{"additionalProperties":false,"properties":{)"
    R"("type":{"enum":["wire"],"type":"string"}},"required":["type"],)"
    R"("type":"object","x-fee":25},{"additionalProperties":false,"properties":{)"
    R"("type":{"enum":["cash"],"type":"string"}},"required":["type"],"type":"object"}],)"
    R"("x-audited":true}},"required":["back","by"],"type":"object"})");

// ---- Reading a keyword's value -------------------------------------------------

using scrydantic::detail::compact_json;

// What compact_json() makes of text that is one JSON value.
constexpr bool compacts_to(const std::string_view text,
                           const std::string_view compact) {
  const auto value = compact_json(text);
  return value.has_value() && *value == compact;
}

static_assert(compacts_to("null", "null"));
static_assert(compacts_to(" true\n", "true"));
static_assert(compacts_to("\tfalse ", "false"));
static_assert(compacts_to("0", "0"));
static_assert(compacts_to("-0", "-0"));
static_assert(compacts_to("-12.50e+3", "-12.50e+3"));
static_assert(compacts_to("1E-7", "1E-7"));
static_assert(compacts_to(R"("")", R"("")"));
static_assert(compacts_to(R"(" a  b ")", R"(" a  b ")"));
static_assert(compacts_to(R"("\"\\\/\b\f\n\r\t\u00e9\uD83D\uDE00")",
                          R"("\"\\\/\b\f\n\r\t\u00e9\uD83D\uDE00")"));
static_assert(compacts_to("\"\xC3\xA9\"", "\"\xC3\xA9\""));
static_assert(compacts_to("[ ]", "[]"));
static_assert(compacts_to("{ }", "{}"));
static_assert(compacts_to(" [ 1 , [ 2 , 3 ] , { \"a\" : [ ] } ] ",
                          R"([1,[2,3],{"a":[]}])"));
// Neither sorted nor merged: the value is the author's text.
static_assert(compacts_to(R"({ "b" : 1, "a" : 2, "b" : 3 })",
                          R"({"b":1,"a":2,"b":3})"));
// The largest double and the smallest, and the last decimals that round to them.
static_assert(compacts_to("1.7976931348623157e308", "1.7976931348623157e308"));
static_assert(compacts_to("1.7976931348623158e308", "1.7976931348623158e308"));
static_assert(compacts_to("5e-324", "5e-324"));
static_assert(compacts_to("2.5e-324", "2.5e-324"));
static_assert(compacts_to("0e999999999999", "0e999999999999"));
static_assert(compacts_to("0.000e-999999999999", "0.000e-999999999999"));
static_assert(compacts_to("123456789012345678901234567890",
                          "123456789012345678901234567890"));

// Text that is one JSON value, and text that is not, for whatever reason the
// parser rejects it.
inline constexpr std::array<std::string_view, 36> valid_values{
    "null",
    "true",
    "false",
    "0",
    "-0",
    "7",
    "-12.50e+3",
    "1E-7",
    "0.0",
    "1.7976931348623157e308",
    "1.7976931348623158e308",
    "5e-324",
    "2.5e-324",
    "9.9e307",
    "1e308",
    "1e-322",
    "3e-324",
    "0.0000000001e318",
    "100000000000e-334",
    "0e999999999999",
    "0.000e-999999999999",
    "123456789012345678901234567890",
    R"("")",
    R"("text")",
    R"("\"\\\/\b\f\n\r\t\u00e9\uD83D\uDE00\u0000")",
    "\"\xC3\xA9\xF0\x9F\x98\x80\"",
    "\"\x7F\"",
    "[]",
    "{}",
    "[[[[]]]]",
    R"([1,"two",null,true,{"three":3}])",
    R"({"a":{"b":{"c":[]}}})",
    R"({"b":1,"a":2,"b":3})",
    " \t\r\n[ 1 , 2 ]\n",
    R"({ "key" : "value" })",
    R"({"":0})",
};

inline constexpr std::array<std::string_view, 62> invalid_values{
    "",
    " ",
    "nul",
    "nulll",
    "True",
    "tru e",
    "undefined",
    "NaN",
    "Infinity",
    "+1",
    "-",
    "01",
    "-01",
    "1.",
    ".5",
    "1.e3",
    "1e",
    "1e+",
    "0x10",
    "1 2",
    "1,",
    "1e999",
    "-1e999",
    "1.7976931348623159e308",
    "1e-999",
    "1e309",
    "1e-324",
    "9e99999999999999999999",
    "9e-99999999999999999999",
    "2.4703282292062327e-324",
    "0.1e-999999999999",
    "'text'",
    "\"open",
    "\"\\\"",
    R"("\x41")",
    R"("\u12")",
    R"("\u12G4")",
    R"("\uD83D")",
    R"("\uD83Dx")",
    R"("\uD83D\u0041")",
    R"("\uDE00")",
    R"("\uDE00\uD83D")",
    "\"line\nbreak\"",
    "\"tab\there\"",
    "\"\xC3\"",
    "\"\xFF\"",
    "\"\xED\xA0\x80\"",
    "\xEF\xBB\xBFnull",
    "[",
    "]",
    "[1,]",
    "[,1]",
    "[1 2]",
    "[1}",
    "{",
    R"({"a"})",
    R"({"a":})",
    R"({"a":1,})",
    R"({a:1})",
    R"({1:1})",
    R"({"a":1 "b":2})",
    R"({"a":1}{})",
};

template <std::size_t Count>
constexpr bool all_compact(const std::array<std::string_view, Count>& texts,
                           const bool expected) {
  for (const auto text : texts) {
    if (compact_json(text).has_value() != expected) {
      return false;
    }
  }
  return true;
}

static_assert(all_compact(valid_values, true));
static_assert(all_compact(invalid_values, false));

// `count` arrays, one inside the other.
constexpr std::string nested_arrays(const std::size_t count) {
  return std::string(count, '[') + std::string(count, ']');
}

static_assert(compact_json(nested_arrays(8)).has_value());

// ---- Tools ---------------------------------------------------------------------

namespace account_tools {

[[= scrydantic::tool{"Open an account"}]] std::string open(Account account) {
  return account.name;
}

} // namespace account_tools

namespace query_tools {

[[= scrydantic::tool{"Run a query"}]] std::string run(Routed query) {
  return query.region;
}

} // namespace query_tools

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

struct NotJson {
  [[= keyword{"examples", R"(["us-west1",])"}]] std::string zone;
};

struct NotOneValue {
  [[= keyword{"examples", R"("a" "b")"}]] std::string zone;
};

struct BareText {
  [[= keyword{"format", "date-time"}]] std::string since;
};

struct NoValue {
  [[= keyword{"deprecated", ""}]] std::string since;
};

struct SameKey {
  [[ = keyword{"x-a", "1"}, = keyword{"x-b", "2"},
     = keyword{"x-a", "1"} ]] std::int32_t value{};
};

struct SameKeyAcrossKinds {
  [[ = keyword{"format", R"("date")"},
     = keyword_string{"format", "date"} ]] std::string day;
};

struct[[ = keyword{"x-a", "1"}, = keyword{"x-a", "2"} ]] SameKeyOnClass {
  std::int32_t value{};
};

struct ReservedType {
  [[= keyword{"type", R"("number")"}]] std::int32_t value{};
};

// One fixed list: "minimum" is reserved on a member whose own schema never
// writes it, and "$ref" on a class that nothing refers to.
struct ReservedElsewhere {
  [[= keyword{"minimum", "0"}]] std::string value;
};

struct ReservedDefault {
  [[= keyword_string{"default", "x"}]] std::string value;
};

struct[[= keyword{"$ref", R"("#/elsewhere")"}]] ReservedOnClass {
  std::int32_t value{};
};

struct EmptyKey {
  [[= keyword{"", "1"}]] std::int32_t value{};
};

static_assert(mentions(scrydantic::supported_value_diagnostic<NotJson>(),
                       "NotJson::zone: carries a scrydantic::keyword with the key "
                       "\"examples\" whose value is not one JSON value"));
static_assert(mentions(scrydantic::supported_value_diagnostic<NotOneValue>(),
                       "whose value is not one JSON value"));
static_assert(mentions(scrydantic::supported_value_diagnostic<BareText>(),
                       "BareText::since: carries a scrydantic::keyword with the key "
                       "\"format\" whose value is not one JSON value"));
static_assert(mentions(scrydantic::supported_value_diagnostic<NoValue>(),
                       "whose value is not one JSON value"));
static_assert(mentions(scrydantic::supported_value_diagnostic<SameKey>(),
                       "SameKey::value: has more than one keyword annotation with the "
                       "key \"x-a\""));
static_assert(mentions(scrydantic::supported_value_diagnostic<SameKeyAcrossKinds>(),
                       "SameKeyAcrossKinds::day: has more than one keyword annotation "
                       "with the key \"format\""));
static_assert(mentions(scrydantic::supported_value_diagnostic<SameKeyOnClass>(),
                       "SameKeyOnClass has more than one keyword annotation with the "
                       "key \"x-a\""));
static_assert(mentions(scrydantic::supported_value_diagnostic<ReservedType>(),
                       "ReservedType::value: carries a keyword annotation with the key "
                       "\"type\", which Scrydantic writes itself; an annotation cannot "
                       "replace a generated keyword"));
static_assert(mentions(scrydantic::supported_value_diagnostic<ReservedElsewhere>(),
                       "carries a keyword annotation with the key \"minimum\", which "
                       "Scrydantic writes itself"));
static_assert(mentions(scrydantic::supported_value_diagnostic<ReservedDefault>(),
                       "carries a keyword annotation with the key \"default\", which "
                       "Scrydantic writes itself"));
static_assert(mentions(scrydantic::supported_value_diagnostic<ReservedOnClass>(),
                       "ReservedOnClass carries a keyword annotation with the key "
                       "\"$ref\", which Scrydantic writes itself"));
static_assert(mentions(scrydantic::supported_value_diagnostic<EmptyKey>(),
                       "EmptyKey::value: carries a keyword annotation with an empty "
                       "key"));
static_assert(mentions(scrydantic::encodable_diagnostic<NotJson>(),
                       "whose value is not one JSON value"));
static_assert(mentions(scrydantic::decodable_diagnostic<ReservedType>(),
                       "which Scrydantic writes itself"));

// Every keyword that some schema in the tests above writes of its own accord is
// reserved, and the list is in the order a schema writes them.
static_assert(std::ranges::is_sorted(scrydantic::detail::reserved_keywords));

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

TEST_CASE("a custom keyword is in the schema and nowhere else") {
  // Neither a format nor an example is checked: "since" is no date.
  const auto query = scrydantic::decode<Query>(
      R"({"region":"eu","since":"last tuesday","zone":"mars-1"})");
  REQUIRE(query);
  CHECK(query->since == "last tuesday");

  const auto encoded = scrydantic::encode(*query);
  REQUIRE(encoded);
  CHECK(encoded->text == R"({"region":"eu","since":"last tuesday","zone":"mars-1"})");

  // A keyword is no member: its key is not one the object reads.
  CHECK_FALSE(scrydantic::decode<Routed>(
      R"({"region":"eu","since":"now","tenant":"t","x-header":"Region"})"));
}

TEST_CASE("a schema with custom keywords is JSON, and canonical where their "
          "values are") {
  for (const std::string_view schema :
       {scrydantic::schema_v<Query>, scrydantic::schema_v<Routed>,
        scrydantic::schema_v<Segment>, scrydantic::schema_v<Folder>,
        scrydantic::schema_v<Drive>, scrydantic::schema_v<Transfer>,
        scrydantic::schema_v<Customer>, scrydantic::schema_v<Thread>}) {
    CAPTURE(schema);
    const auto parsed = scrydantic::JsonView::parse(schema);
    REQUIRE(parsed);
    CHECK(parsed->to_json().text == schema);
  }

  // Gauge's values are not canonical, so its schema is JSON that the canonical
  // writer respells: "a" before "b", and 2.5 for 2.50.
  const std::string_view gauge = scrydantic::schema_v<Gauge>;
  const auto parsed = scrydantic::JsonView::parse(gauge);
  REQUIRE(parsed);
  CHECK(parsed->to_json().text != gauge);
  const auto properties = parsed->find("properties");
  REQUIRE(properties);
  const auto pressure = properties->find("pressure");
  REQUIRE(pressure);
  const auto examples = pressure->find("examples");
  REQUIRE(examples);
  CHECK(examples->to_json().text == R"([1,{"a":[],"b":2.5},null])");
}

TEST_CASE("a keyword's value is read at compile time as the parser reads JSON") {
  for (const auto text : valid_values) {
    CAPTURE(text);
    CHECK(scrydantic::is_valid_json(text));
    CHECK(compact_json(text).has_value());
  }
  for (const auto text : invalid_values) {
    CAPTURE(text);
    CHECK_FALSE(scrydantic::is_valid_json(text));
    CHECK_FALSE(compact_json(text).has_value());
  }
  // A value may nest as deep as the parser reads, and no deeper.
  const auto deepest = nested_arrays(scrydantic::max_json_depth);
  CHECK(scrydantic::is_valid_json(deepest));
  CHECK(compact_json(deepest) == deepest);
  const auto too_deep = nested_arrays(scrydantic::max_json_depth + 1);
  CHECK_FALSE(scrydantic::is_valid_json(too_deep));
  CHECK_FALSE(compact_json(too_deep).has_value());
}

TEST_CASE("a compacted keyword value is the same JSON value") {
  for (const auto text : valid_values) {
    CAPTURE(text);
    const auto compact = compact_json(text);
    REQUIRE(compact);
    const auto written = scrydantic::JsonView::parse(text);
    const auto compacted = scrydantic::JsonView::parse(*compact);
    REQUIRE(written);
    REQUIRE(compacted);
    CHECK(written->to_json().text == compacted->to_json().text);
    CHECK(compact->find_first_of(" \t\r\n") == std::string::npos);
  }
}

TEST_CASE("a tool's argument aggregate carries its keywords into the input "
          "schema") {
  const auto tools = scrydantic::bind<^^query_tools>();
  REQUIRE(tools.size() == 1);
  CHECK(tools[0].input_schema.text == scrydantic::schema_v<Routed>);
  CHECK(tools[0].input_schema.text.contains(
      R"("region":{"type":"string","x-header":"Region"})"));
}
