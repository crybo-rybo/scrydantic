// Variants told apart by JSON kind: a std::variant with at most one alternative
// of each JSON kind, whose value's own kind says which alternative it is. Their
// schema, how a value selects an alternative, the integer and floating-point
// alternatives that share the numbers, the tagged aggregates that share the
// objects, std::monostate as null, where such a variant can stand in a model and
// a tool, and every variant the compiler rejects, with its diagnostic.

#include <array>
#include <catch2/catch_test_macros.hpp>
#include <cstddef>
#include <cstdint>
#include <map>
#include <memory>
#include <meta>
#include <optional>
#include <scrydantic/scrydantic.hpp>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <variant>
#include <vector>

namespace {

using scrydantic::DecodeOptions;
using scrydantic::IssueKind;
using scrydantic::Json;

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

// "A string or a number": the id of a request.
using Id = std::variant<std::int64_t, std::string>;

struct Request {
  Id id;

  bool operator==(const Request&) const = default;
};

// "One item or a list of them."
struct Item {
  std::string name;
  std::int32_t count{1};

  bool operator==(const Item&) const = default;
};

using OneOrMany = std::variant<Item, std::vector<Item>>;

// "A string, or a tagged shape": the aggregates are a tagged union among
// themselves, beside an alternative of another kind.
struct[[= scrydantic::tag{"circle"}]] Circle {
  double radius{};

  bool operator==(const Circle&) const = default;
};

struct[[= scrydantic::tag{"square"}]] Square {
  double side{};

  bool operator==(const Square&) const = default;
};

using Shape = std::variant<Circle, Square>;
using NamedShape = std::variant<std::string, Circle, Square>;

// An integer and a floating-point alternative share the numbers.
using Count = std::variant<std::int32_t, double>;
using Wide = std::variant<std::int64_t, double>;
using Natural = std::variant<float, std::uint64_t>;

enum class Mode { fast, slow[[= scrydantic::name{"careful"}]] };

// One alternative of every kind.
using Every = std::variant<std::monostate, bool, std::int64_t, Mode,
                           std::array<std::int32_t, 2>, std::map<std::string, bool>>;

// "A flag or an options object", the object a plain aggregate with a member
// whose key is "type": only a tagged alternative keeps that key for its tag.
struct Options {
  std::string type;
  bool verbose{};

  bool operator==(const Options&) const = default;
};

using Verbosity = std::variant<bool, Options>;

// Every place a kind-variant can stand in a model.
struct Places {
  [[= scrydantic::description{"Who asked"}]] Id id;
  std::optional<Id> parent;
  std::vector<Id> related;
  std::map<std::string, Count> totals;
  std::variant<std::monostate, std::string> note;
  [[= scrydantic::title{"Outline"}]] NamedShape shape;

  bool operator==(const Places&) const = default;
};

struct[[= scrydantic::skip_null]] Sparse {
  std::optional<Id> id;
};

// Members that keep a default, which a schema states as encode() writes it.
struct Defaults {
  Id id{7};
  Id name{"anonymous"};
  std::variant<std::monostate, std::string> note{};
  NamedShape shape{Circle{.radius = 2}};
  OneOrMany items{std::vector<Item>{{.name = "a"}}};
  std::optional<Id> parent{};
};

} // namespace

namespace kind_variants {

// A model that contains itself through an alternative that is an array.
struct Tree {
  std::variant<std::string, std::vector<Tree>> value;

  bool operator==(const Tree&) const = default;
};

// And through tagged alternatives beside one that is not an object.
struct Sum;
struct Negation;
using Expression = std::variant<double, Sum, Negation>;

struct[[= scrydantic::tag{"sum"}]] Sum {
  std::vector<Expression> terms;
};

struct[[= scrydantic::tag{"negation"}]] Negation {
  std::vector<Expression> of;
};

struct Formula {
  Expression root;
};

} // namespace kind_variants

namespace kind_variant_tools {

[[= scrydantic::tool{"The id after this one"}]] inline Id next(Id id) {
  if (const auto* const number = std::get_if<std::int64_t>(&id)) {
    return *number + 1;
  }
  return std::get<std::string>(id) + "+";
}

[[= scrydantic::tool{"How many items"}]] inline scrydantic::Result<Count>
    total(OneOrMany items, std::optional<Id> owner) {
  if (owner && std::holds_alternative<std::string>(*owner)) {
    return 0.5;
  }
  const auto* const many = std::get_if<std::vector<Item>>(&items);
  return many == nullptr ? 1 : static_cast<std::int32_t>(many->size());
}

} // namespace kind_variant_tools

namespace {

// ---- Families ----------------------------------------------------------------

static_assert(scrydantic::SupportedValue<Id>);
static_assert(scrydantic::Encodable<Id>);
static_assert(scrydantic::Decodable<Id>);
static_assert(scrydantic::SupportedValue<Request>);
static_assert(scrydantic::SupportedValue<OneOrMany>);
static_assert(scrydantic::SupportedValue<NamedShape>);
static_assert(scrydantic::SupportedValue<Count>);
static_assert(scrydantic::SupportedValue<Natural>);
static_assert(scrydantic::SupportedValue<Every>);
static_assert(scrydantic::SupportedValue<Verbosity>);
static_assert(scrydantic::SupportedValue<Places>);
static_assert(scrydantic::SupportedValue<kind_variants::Tree>);
static_assert(scrydantic::SupportedValue<kind_variants::Formula>);
// A variant's only alternative of a kind is told by that kind, so an aggregate
// that stands alone needs no tag, and neither does a variant of one alternative.
static_assert(scrydantic::SupportedValue<std::variant<Item>>);
static_assert(scrydantic::SupportedValue<std::variant<std::string>>);
// A variant is a value, not an argument object.
static_assert(!scrydantic::ToolArguments<Id>);
static_assert(scrydantic::ToolArguments<Request>);

// Encoding alone reads a std::string_view, as an alternative too.
using Borrowed = std::variant<std::string_view, std::int32_t>;
static_assert(scrydantic::Encodable<Borrowed>);
static_assert(!scrydantic::Decodable<Borrowed>);
static_assert(mentions(scrydantic::supported_value_diagnostic<Borrowed>(),
                       " is encode-only borrowed text; decoding and schemas take "
                       "std::string"));

// ---- Schemas -----------------------------------------------------------------

constexpr std::string_view int64_schema =
    R"({"maximum":9223372036854775807,"minimum":-9223372036854775808,"type":"integer"})";
constexpr std::string_view int32_schema =
    R"({"maximum":2147483647,"minimum":-2147483648,"type":"integer"})";
constexpr std::string_view item_schema =
    R"({"additionalProperties":false,"properties":{"count":{"default":1,"maximum":2147483647,"minimum":-2147483648,"type":"integer"},"name":{"type":"string"}},"required":["name"],"type":"object"})";
constexpr std::string_view circle_schema =
    R"({"additionalProperties":false,"properties":{"radius":{"default":0,"type":"number"},"type":{"enum":["circle"],"type":"string"}},"required":["type"],"type":"object"})";
constexpr std::string_view square_schema =
    R"({"additionalProperties":false,"properties":{"side":{"default":0,"type":"number"},"type":{"enum":["square"],"type":"string"}},"required":["type"],"type":"object"})";

// The schemas above, joined as the members of an "anyOf".
template <typename... Schemas>
[[nodiscard]] std::string any_of(const Schemas&... schemas) {
  std::string text{R"({"anyOf":[)"};
  bool first = true;
  for (const std::string_view schema : {std::string_view{schemas}...}) {
    if (!first) {
      text.push_back(',');
    }
    text.append(schema);
    first = false;
  }
  return text + "]}";
}

static_assert(
    scrydantic::schema_v<Request> ==
    R"({"additionalProperties":false,"properties":{"id":{"anyOf":[{"maximum":9223372036854775807,"minimum":-9223372036854775808,"type":"integer"},{"type":"string"}]}},"required":["id"],"type":"object"})");
static_assert(scrydantic::input_schema_v<Request> == scrydantic::schema_v<Request>);
static_assert(
    scrydantic::schema_v<Id> ==
    R"({"anyOf":[{"maximum":9223372036854775807,"minimum":-9223372036854775808,"type":"integer"},{"type":"string"}]})");
// An integer is valid against both of these, which "anyOf" allows.
static_assert(
    scrydantic::schema_v<Count> ==
    R"({"anyOf":[{"maximum":2147483647,"minimum":-2147483648,"type":"integer"},{"type":"number"}]})");
// Alternative order, whatever the kinds.
static_assert(
    scrydantic::schema_v<Natural> ==
    R"({"anyOf":[{"type":"number"},{"maximum":18446744073709551615,"minimum":0,"type":"integer"}]})");
static_assert(
    scrydantic::schema_v<Every> ==
    R"({"anyOf":[{"type":"null"},{"type":"boolean"},{"maximum":9223372036854775807,"minimum":-9223372036854775808,"type":"integer"},{"enum":["fast","careful"],"type":"string"},{"items":{"maximum":2147483647,"minimum":-2147483648,"type":"integer"},"maxItems":2,"minItems":2,"type":"array"},{"additionalProperties":{"type":"boolean"},"type":"object"}]})");
// A lone aggregate without a tag is the plain object it is anywhere.
static_assert(
    scrydantic::schema_v<Verbosity> ==
    R"({"anyOf":[{"type":"boolean"},{"additionalProperties":false,"properties":{"type":{"type":"string"},"verbose":{"default":false,"type":"boolean"}},"required":["type"],"type":"object"}]})");
// An optional joins null to the variant's own anyOf, as it does for a tagged
// union.
static_assert(
    scrydantic::schema_v<std::optional<Id>> ==
    R"({"anyOf":[{"maximum":9223372036854775807,"minimum":-9223372036854775808,"type":"integer"},{"type":"string"},{"type":"null"}]})");
// A member's own keywords stand beside the anyOf.
static_assert(mentions(scrydantic::schema_v<Places>,
                       R"(],"description":"Who asked"})"));
static_assert(mentions(scrydantic::schema_v<Places>, R"(],"title":"Outline"})"));
static_assert(mentions(scrydantic::schema_v<Places>,
                       R"("note":{"anyOf":[{"type":"null"},{"type":"string"}]})"));
// A recursive root refers to itself from inside an alternative.
static_assert(
    scrydantic::schema_v<kind_variants::Tree> ==
    R"({"additionalProperties":false,"properties":{"value":{"anyOf":[{"type":"string"},{"items":{"$ref":"#"},"type":"array"}]}},"required":["value"],"type":"object"})");
// Tagged alternatives that contain the variant are definitions, as they are in
// a tagged union.
static_assert(
    scrydantic::schema_v<kind_variants::Formula> ==
    R"({"$defs":{"kind_variants.Negation":{"additionalProperties":false,"properties":{"of":{"items":{"anyOf":[{"type":"number"},{"$ref":"#/$defs/kind_variants.Sum"},{"$ref":"#/$defs/kind_variants.Negation"}]},"type":"array"},"type":{"enum":["negation"],"type":"string"}},"required":["of","type"],"type":"object"},"kind_variants.Sum":{"additionalProperties":false,"properties":{"terms":{"items":{"anyOf":[{"type":"number"},{"$ref":"#/$defs/kind_variants.Sum"},{"$ref":"#/$defs/kind_variants.Negation"}]},"type":"array"},"type":{"enum":["sum"],"type":"string"}},"required":["terms","type"],"type":"object"}},"additionalProperties":false,"properties":{"root":{"anyOf":[{"type":"number"},{"$ref":"#/$defs/kind_variants.Sum"},{"$ref":"#/$defs/kind_variants.Negation"}]}},"required":["root"],"type":"object"})");

// A default is the alternative that the member's initializer holds.
static_assert(mentions(scrydantic::schema_v<Defaults>, R"(,"default":7})"));
static_assert(mentions(scrydantic::schema_v<Defaults>, R"(,"default":"anonymous"})"));
static_assert(mentions(scrydantic::schema_v<Defaults>,
                       R"("note":{"anyOf":[{"type":"null"},{"type":"string"}],)"
                       R"("default":null})"));
static_assert(mentions(scrydantic::schema_v<Defaults>,
                       R"(,"default":{"radius":2,"type":"circle"}})"));
static_assert(mentions(scrydantic::schema_v<Defaults>,
                       R"(,"default":[{"count":1,"name":"a"}]})"));
static_assert(mentions(scrydantic::schema_v<Defaults>, R"("required":[])"));

// ---- Rejected variants -------------------------------------------------------

struct Other {
  bool flag{};
};

struct Bounded {
  [[= scrydantic::range{.min = 1}]] Id id;
};

struct Lengthy {
  [[= scrydantic::length{.max = 4}]] std::optional<Id> id;
};

struct TypedCircle {
  std::variant<std::string, Circle, Square> shape;
  std::variant<std::int32_t, std::vector<wchar_t>> letters;
};

// Two alternatives of one JSON kind: no value could select between them.
static_assert(
    mentions(scrydantic::supported_value_diagnostic<std::variant<std::string, Mode>>(),
             " that share the JSON kind string; the kind of a JSON value selects its "
             "alternative, and only aggregates that each carry a scrydantic::tag can "
             "share one"));
static_assert(
    mentions(scrydantic::supported_value_diagnostic<
                 std::variant<std::vector<std::int32_t>, std::vector<std::string>>>(),
             " that share the JSON kind array"));
static_assert(
    mentions(scrydantic::supported_value_diagnostic<
                 std::variant<bool, std::vector<std::int32_t>, std::array<bool, 2>>>(),
             " that share the JSON kind array"));
static_assert(mentions(
    scrydantic::encodable_diagnostic<std::variant<std::int32_t, std::int64_t>>(),
    "has alternatives int and long int that share the JSON kind integer"));
static_assert(mentions(scrydantic::decodable_diagnostic<std::variant<float, double>>(),
                       "has alternatives float and double that share the JSON kind "
                       "number"));
static_assert(
    mentions(scrydantic::supported_value_diagnostic<std::variant<bool, double, bool>>(),
             "has alternatives bool and bool that share the JSON kind boolean"));
static_assert(mentions(scrydantic::supported_value_diagnostic<
                           std::variant<std::monostate, std::monostate>>(),
                       " that share the JSON kind null"));
// Objects are shared by tagged aggregates alone: an aggregate without a tag is
// told what it lacks, whichever side of the other it stands on.
static_assert(
    mentions(scrydantic::supported_value_diagnostic<std::variant<Item, Other>>(),
             "Item without a scrydantic::tag annotation beside {anonymous}::Other, "
             "another JSON object; alternatives that are objects are told apart by "
             "their tags"));
static_assert(mentions(
    scrydantic::supported_value_diagnostic<std::variant<std::string, Circle, Item>>(),
    "Item without a scrydantic::tag annotation beside {anonymous}::Circle, another "
    "JSON object"));
static_assert(mentions(scrydantic::supported_value_diagnostic<
                           std::variant<std::map<std::string, bool>, Circle>>(),
                       "Circle that share the JSON kind object"));
static_assert(
    mentions(scrydantic::supported_value_diagnostic<std::variant<
                 std::map<std::string, bool>, std::unordered_map<std::string, bool>>>(),
             " that share the JSON kind object"));
static_assert(mentions(
    scrydantic::supported_value_diagnostic<std::variant<std::string, Circle, Circle>>(),
    "Circle that share the tag \"circle\""));
// Types with no JSON kind of their own.
static_assert(mentions(
    scrydantic::supported_value_diagnostic<std::variant<std::string, Json>>(),
    "has the alternative scrydantic::Json, which holds a value of any JSON kind, so "
    "no kind selects it; a scrydantic::Json in the variant's place holds what the "
    "variant would"));
static_assert(mentions(
    scrydantic::supported_value_diagnostic<
        std::variant<std::string, std::optional<std::int32_t>>>(),
    "has the alternative std::optional<int>, which is null or its value, so it has "
    "no one JSON kind; std::monostate is the alternative that is null"));
static_assert(mentions(
    scrydantic::supported_value_diagnostic<std::variant<std::string, Shape>>(),
    "has the alternative std::variant<{anonymous}::Circle, {anonymous}::Square>, "
    "which is a variant itself, so it has no one JSON kind; list its alternatives "
    "in this variant"));
static_assert(
    mentions(scrydantic::supported_value_diagnostic<
                 std::variant<std::string, std::indirect<Item>>>(),
             ", which is a std::indirect, which a variant does not see through; an "
             "aggregate alternative can hold it as a member"));
// A cv-qualified alternative is told so where the variant is only read. Where
// it is decoded, it is told first that such a variant cannot be assigned.
static_assert(
    mentions(scrydantic::encodable_diagnostic<std::variant<std::string, const Item>>(),
             "Item, which is cv-qualified; an alternative is a plain value type"));
static_assert(
    mentions(scrydantic::decodable_diagnostic<std::variant<std::string, const Item>>(),
             " must be default-constructible, move-constructible, and "
             "move-assignable"));
static_assert(
    mentions(scrydantic::supported_value_diagnostic<std::variant<std::string, int*>>(),
             "int* is not a supported reflected value"));
// An alternative is checked as its type is anywhere, at a path that names it.
static_assert(mentions(scrydantic::supported_value_diagnostic<TypedCircle>(),
                       "TypedCircle::letters<std::vector<wchar_t>>[]: wchar_t is a "
                       "character type"));
// Null has one meaning in each place.
static_assert(
    mentions(scrydantic::supported_value_diagnostic<
                 std::optional<std::variant<std::monostate, std::int32_t>>>(),
             " holds a variant with a std::monostate alternative, so null would be "
             "both the empty optional and that alternative; keep one of the two"));
// A constraint bounds a value of one type, which a variant is not.
static_assert(mentions(scrydantic::supported_value_diagnostic<Bounded>(),
                       "Bounded::id: carries scrydantic::range, which applies to "
                       "integer and floating-point members, not to std::variant<"));
static_assert(mentions(scrydantic::supported_value_diagnostic<Lengthy>(),
                       "Lengthy::id: carries scrydantic::length, which applies to "
                       "std::string members, not to std::optional<"));

// tags_v lists a tag for every alternative, so only a tagged union has one.
static_assert(scrydantic::tags_v<Shape>.size() == 2);
static_assert(scrydantic::tags_v<Shape>[1] == "square");

// ---- Helpers -----------------------------------------------------------------

template <typename Enum> [[nodiscard]] std::string enumerator_name(const Enum value) {
  template for (constexpr std::meta::info enumerator :
                std::define_static_array(std::meta::enumerators_of(^^Enum))) {
    if (value == [:enumerator:]) {
      return std::string{std::meta::identifier_of(enumerator)};
    }
  }
  return "an undeclared enumerator";
}

using Lines = std::vector<std::string>;

// The issues of a decode that must fail, each as `kind path reason`.
template <typename Type>
[[nodiscard]] Lines rejection(const std::string_view text,
                              const DecodeOptions& options = {}) {
  const auto decoded = scrydantic::decode<Type>(text, options);
  REQUIRE_FALSE(decoded);
  Lines lines{};
  for (const auto& issue : decoded.error().issues) {
    lines.push_back(enumerator_name(issue.kind) + " " +
                    scrydantic::json_path(issue.path) + " " + issue.reason);
  }
  return lines;
}

// The index of the alternative that `text` decodes as, which must succeed, and
// that the value encodes as `text` again.
template <typename Variant>
[[nodiscard]] std::size_t selected(const std::string_view text,
                                   const DecodeOptions& options = {}) {
  CAPTURE(text);
  const auto decoded = scrydantic::decode<Variant>(text, options);
  REQUIRE(decoded);
  return decoded->index();
}

template <typename Type> [[nodiscard]] std::string encoded(const Type& value) {
  const auto json = scrydantic::encode(value);
  REQUIRE(json);
  return json->text;
}

// ---- Selecting by kind -------------------------------------------------------

TEST_CASE("an integer-or-string member decodes either and round trips") {
  const auto number = scrydantic::decode<Request>(R"({"id":7})");
  REQUIRE(number);
  CHECK(*number == Request{.id = std::int64_t{7}});
  CHECK(encoded(*number) == R"({"id":7})");

  const auto text = scrydantic::decode<Request>(R"({"id":"abc"})");
  REQUIRE(text);
  CHECK(*text == Request{.id = std::string{"abc"}});
  CHECK(encoded(*text) == R"({"id":"abc"})");

  CHECK(encoded(Request{.id = std::string{"7"}}) == R"({"id":"7"})");
  CHECK(std::get<std::string>(scrydantic::decode<Request>(R"({"id":"7"})")->id) == "7");
}

TEST_CASE("a value of a kind no alternative has is one issue that lists the kinds") {
  const auto refused = scrydantic::decode<Request>(R"({"id":true})");
  REQUIRE_FALSE(refused);
  CHECK(refused.error().model_message == "$.id must be an integer or a string");
  REQUIRE(refused.error().issues.size() == 1);
  CHECK(refused.error().issues[0].kind == IssueKind::wrong_type);

  // Kinds are listed once each, in alternative order.
  CHECK(rejection<Id>("null") == Lines{"wrong_type $ must be an integer or a string"});
  CHECK(rejection<Id>("[7]") == Lines{"wrong_type $ must be an integer or a string"});
  CHECK(rejection<OneOrMany>("7") ==
        Lines{"wrong_type $ must be an object or an array"});
  CHECK(rejection<NamedShape>("7") ==
        Lines{"wrong_type $ must be a string or an object"});
  CHECK(rejection<Count>("true") ==
        Lines{"wrong_type $ must be an integer or a number"});
  CHECK(rejection<Natural>("{}") ==
        Lines{"wrong_type $ must be a number or an integer"});
  CHECK(rejection<Every>("1.5") ==
        Lines{"wrong_type $ must be null, a boolean, an integer, a string, an array, "
              "or an object"});
  CHECK(rejection<std::variant<std::monostate, bool, std::string>>("1") ==
        Lines{"wrong_type $ must be null, a boolean, or a string"});
  CHECK(rejection<std::variant<std::string>>("1") ==
        Lines{"wrong_type $ must be a string"});
  // A tagged union is the variant whose every alternative is an object.
  CHECK(rejection<Shape>("1") == Lines{"wrong_type $ must be an object"});
}

TEST_CASE("each JSON kind selects the alternative of that kind") {
  CHECK(selected<Every>("null") == 0);
  CHECK(selected<Every>("false") == 1);
  CHECK(selected<Every>("-3") == 2);
  CHECK(selected<Every>("3") == 2);
  CHECK(selected<Every>(R"("careful")") == 3);
  CHECK(selected<Every>("[1,2]") == 4);
  CHECK(selected<Every>(R"({"a":true})") == 5);

  for (const std::string_view text :
       {"null", "false", "-3", R"("careful")", "[1,2]", R"({"a":true,"b":false})"}) {
    CAPTURE(text);
    const auto decoded = scrydantic::decode<Every>(text);
    REQUIRE(decoded);
    CHECK(encoded(*decoded) == text);
  }
  CHECK(encoded(Every{}) == "null");
  CHECK(encoded(Every{Mode::fast}) == R"("fast")");
}

TEST_CASE("the selected alternative reports its own issues at the usual paths") {
  // The string alternative is an enum, so a string is held to its enumerators.
  CHECK(rejection<Every>(R"("slow")") ==
        Lines{"unknown_enumerator $ is not a declared enumerator; must be one of: "
              "fast, careful"});
  CHECK(rejection<Every>("[1,2,3]") ==
        Lines{"wrong_size $ must be an array of the declared fixed size"});
  CHECK(rejection<Every>(R"({"a":1,"b":true,"c":null})") ==
        Lines{"wrong_type $.a must be a boolean", "wrong_type $.c must be a boolean"});
  CHECK(rejection<Request>(R"({"id":9223372036854775808})") ==
        Lines{"out_of_range $.id is outside the integer range"});
  CHECK(rejection<OneOrMany>(R"([{"name":"a"},{"name":1,"extra":2},7])") ==
        Lines{"unknown_member $[1] contains unknown member \"extra\"",
              "wrong_type $[1].name must be a string",
              "wrong_type $[2] must be an object"});
  CHECK(rejection<OneOrMany>(R"({"count":"2"})") ==
        Lines{"missing_member $.name is a required member",
              "wrong_type $.count must be an integer"});
}

// ---- Integer or floating point -----------------------------------------------

TEST_CASE("an integer that fits selects the integer alternative, any other number "
          "the floating-point one") {
  CHECK(selected<Count>("1") == 0);
  CHECK(selected<Count>("-1") == 0);
  CHECK(selected<Count>("1.5") == 1);
  // The integer alternative's range, and one past each end of it.
  CHECK(selected<Count>("2147483647") == 0);
  CHECK(selected<Count>("-2147483648") == 0);
  CHECK(selected<Count>("2147483648") == 1);
  CHECK(selected<Count>("-2147483649") == 1);
  CHECK(std::get<double>(*scrydantic::decode<Count>("2147483648")) == 2147483648.0);
  // A fraction or an exponent makes a number that is not a JSON integer.
  CHECK(selected<Count>("1.0") == 1);
  CHECK(selected<Count>("1e2") == 1);
  CHECK(selected<Count>("-0") == 1);

  CHECK(selected<Wide>("9223372036854775807") == 0);
  CHECK(selected<Wide>("-9223372036854775808") == 0);
  // Past the 64-bit integer alternative, and then past every 64-bit integer.
  CHECK(selected<Wide>("9223372036854775808") == 1);
  CHECK(selected<Wide>("-9223372036854775809") == 1);
  CHECK(selected<Wide>("18446744073709551616") == 1);
  CHECK(std::get<double>(*scrydantic::decode<Wide>("18446744073709551616")) == 0x1p64);

  // An unsigned alternative takes no negative integer.
  CHECK(selected<Natural>("18446744073709551615") == 1);
  CHECK(selected<Natural>("0") == 1);
  CHECK(selected<Natural>("-1") == 0);
  CHECK(selected<Natural>("0.5") == 0);
}

TEST_CASE("a number too large for a double is not JSON, whatever would read it") {
  const auto refused = scrydantic::decode<Count>("1e400");
  REQUIRE_FALSE(refused);
  REQUIRE(refused.error().issues.size() == 1);
  CHECK(refused.error().issues[0].kind == IssueKind::invalid_json);
  // A float alternative has a range of its own.
  CHECK(rejection<Natural>("1e39") ==
        Lines{"out_of_range $ must be a finite in-range number"});
}

TEST_CASE("without a floating-point alternative an integer is held to its range") {
  CHECK(rejection<Id>("9223372036854775808") ==
        Lines{"out_of_range $ is outside the integer range"});
  CHECK(rejection<Id>("1.5") == Lines{"wrong_type $ must be an integer or a string"});
  CHECK(rejection<Id>("18446744073709551616") ==
        Lines{"wrong_type $ must be an integer or a string"});
  // And without an integer alternative every number is the floating-point one.
  CHECK(selected<std::variant<std::string, double>>("7") == 1);
}

TEST_CASE("a whole floating-point value is written as an integer, and reads back as "
          "one") {
  CHECK(encoded(Count{1.5}) == "1.5");
  CHECK(encoded(Count{std::int32_t{2}}) == "2");
  // JSON has one `2`, and it is within the integer alternative's range.
  CHECK(encoded(Count{2.0}) == "2");
  CHECK(selected<Count>(encoded(Count{2.0})) == 0);
  CHECK(selected<Count>(encoded(Count{4294967296.0})) == 1);
}

// ---- One or many ---------------------------------------------------------------

TEST_CASE("one item or a list of them") {
  const auto one = scrydantic::decode<OneOrMany>(R"({"name":"a"})");
  REQUIRE(one);
  CHECK(std::get<Item>(*one) == Item{.name = "a"});
  CHECK(encoded(*one) == R"({"count":1,"name":"a"})");

  const auto many =
      scrydantic::decode<OneOrMany>(R"([{"name":"a"},{"count":3,"name":"b"}])");
  REQUIRE(many);
  CHECK(std::get<std::vector<Item>>(*many) ==
        std::vector<Item>{{.name = "a"}, {.name = "b", .count = 3}});
  CHECK(encoded(*many) == R"([{"count":1,"name":"a"},{"count":3,"name":"b"}])");

  CHECK(selected<OneOrMany>("[]") == 1);
  CHECK(scrydantic::schema_v<OneOrMany> ==
        any_of(item_schema,
               R"({"items":)" + std::string{item_schema} + R"(,"type":"array"})"));
}

// ---- Tagged aggregates beside another kind -----------------------------------

TEST_CASE("a string or a tagged aggregate") {
  CHECK(scrydantic::schema_v<NamedShape> ==
        any_of(R"({"type":"string"})", circle_schema, square_schema));

  const auto named = scrydantic::decode<NamedShape>(R"("unit")");
  REQUIRE(named);
  CHECK(std::get<std::string>(*named) == "unit");
  CHECK(encoded(*named) == R"("unit")");

  const auto circle = scrydantic::decode<NamedShape>(R"({"type":"circle","radius":2})");
  REQUIRE(circle);
  CHECK(std::get<Circle>(*circle) == Circle{.radius = 2});
  CHECK(encoded(*circle) == R"({"radius":2,"type":"circle"})");
  CHECK(selected<NamedShape>(R"({"side":1,"type":"square"})") == 2);

  // An object is held to the tags, exactly as a tagged union holds it.
  CHECK(rejection<NamedShape>(R"({"radius":2})") ==
        Lines{"missing_tag $.type is a required member; must be one of: circle, "
              "square"});
  CHECK(rejection<NamedShape>(R"({"type":"oval"})") ==
        Lines{"unknown_tag $.type must be one of: circle, square"});
  CHECK(rejection<NamedShape>(R"({"type":7})") ==
        Lines{"unknown_tag $.type must be one of: circle, square"});
  CHECK(rejection<NamedShape>(R"({"type":"square","side":"1","width":2})") ==
        Lines{"unknown_member $ contains unknown member \"width\"",
              "wrong_type $.side must be a number"});
  // A string is the string alternative, and never a tag.
  CHECK(selected<NamedShape>(R"("circle")") == 0);
}

TEST_CASE("a tagged union is the variant whose every alternative is an object") {
  // The same alternatives encode, decode, and fail alike with or without an
  // alternative of another kind beside them.
  CHECK(scrydantic::schema_v<Shape> == any_of(circle_schema, square_schema));
  for (const std::string_view text :
       {R"({"radius":2,"type":"circle"})", R"({"side":1.5,"type":"square"})"}) {
    CAPTURE(text);
    const auto alone = scrydantic::decode<Shape>(text);
    const auto beside = scrydantic::decode<NamedShape>(text);
    REQUIRE(alone);
    REQUIRE(beside);
    CHECK(encoded(*alone) == text);
    CHECK(encoded(*beside) == text);
  }
  for (const std::string_view text :
       {R"({"radius":2})", R"({"type":"oval"})", R"({"type":"circle","side":1})"}) {
    CAPTURE(text);
    CHECK(rejection<Shape>(text) == rejection<NamedShape>(text));
  }
}

TEST_CASE("a lone aggregate without a tag is a plain object, with any keys") {
  const auto options =
      scrydantic::decode<Verbosity>(R"({"type":"loud","verbose":true})");
  REQUIRE(options);
  CHECK(std::get<Options>(*options) == Options{.type = "loud", .verbose = true});
  CHECK(encoded(*options) == R"({"type":"loud","verbose":true})");
  CHECK(selected<Verbosity>("true") == 0);
  CHECK(rejection<Verbosity>(R"({"verbose":true})") ==
        Lines{"missing_member $.type is a required member"});

  // A variant of one alternative is that alternative.
  CHECK(encoded(std::variant<Item>{Item{.name = "a"}}) == R"({"count":1,"name":"a"})");
  CHECK(selected<std::variant<Item>>(R"({"name":"a"})") == 0);
}

// ---- Null --------------------------------------------------------------------

TEST_CASE("std::monostate is null") {
  using Note = std::variant<std::monostate, std::string>;
  CHECK(scrydantic::schema_v<Note> ==
        any_of(R"({"type":"null"})", R"({"type":"string"})"));
  CHECK(encoded(Note{}) == "null");
  CHECK(encoded(Note{"hello"}) == R"("hello")");
  CHECK(selected<Note>("null") == 0);
  CHECK(selected<Note>(R"("hello")") == 1);
  CHECK(rejection<Note>("0") == Lines{"wrong_type $ must be null or a string"});

  // It need not come first: the variant then starts as another alternative.
  using Late = std::variant<std::int64_t, std::monostate>;
  CHECK(encoded(Late{}) == "0");
  CHECK(encoded(Late{std::monostate{}}) == "null");
  CHECK(selected<Late>("null") == 1);

  // A member that is such a variant is required, and null is a value of it.
  struct Holder {
    Note note;
  };
  CHECK(encoded(Holder{}) == R"({"note":null})");
  CHECK(scrydantic::decode<Holder>(R"({"note":null})"));
  CHECK(rejection<Holder>("{}") == Lines{"missing_member $.note is a required member"});
}

// ---- Places ------------------------------------------------------------------

TEST_CASE("a kind-variant nests in a vector, an optional, and a map") {
  const std::string text =
      R"({"id":"a-1","note":null,"parent":7,"related":[1,"two",3],)"
      R"("shape":{"radius":1,"type":"circle"},"totals":{"half":0.5,"whole":2}})";
  const auto decoded = scrydantic::decode<Places>(text);
  REQUIRE(decoded);
  CHECK(std::get<std::string>(decoded->id) == "a-1");
  REQUIRE(decoded->parent.has_value());
  CHECK(std::get<std::int64_t>(*decoded->parent) == 7);
  REQUIRE(decoded->related.size() == 3);
  CHECK(std::get<std::string>(decoded->related[1]) == "two");
  CHECK(std::get<double>(decoded->totals.at("half")) == 0.5);
  CHECK(std::get<std::int32_t>(decoded->totals.at("whole")) == 2);
  CHECK(encoded(*decoded) == text);

  // Null is the optional's, since no alternative is.
  const auto empty = scrydantic::decode<std::optional<Id>>("null");
  REQUIRE(empty);
  CHECK_FALSE(empty->has_value());
  CHECK(encoded(std::optional<Id>{}) == "null");
  CHECK(encoded(std::optional<Id>{Id{"x"}}) == R"("x")");
  CHECK(encoded(std::vector<Id>{Id{std::int64_t{1}}, Id{"x"}}) == R"([1,"x"])");

  CHECK(rejection<Places>(R"({"id":1.5,"note":0,"parent":false,"related":[1,null,[]],)"
                          R"("shape":[],"totals":{"a":"1"}})") ==
        Lines{"wrong_type $.id must be an integer or a string",
              "wrong_type $.parent must be an integer or a string",
              "wrong_type $.related[1] must be an integer or a string",
              "wrong_type $.related[2] must be an integer or a string",
              "wrong_type $.totals.a must be an integer or a number",
              "wrong_type $.note must be null or a string",
              "wrong_type $.shape must be a string or an object"});
}

TEST_CASE("skip_null leaves out an absent kind-variant, and reads its absence") {
  CHECK(encoded(Sparse{}) == "{}");
  CHECK(encoded(Sparse{.id = Id{"x"}}) == R"({"id":"x"})");
  const auto absent = scrydantic::decode<Sparse>("{}");
  REQUIRE(absent);
  CHECK_FALSE(absent->id.has_value());
}

TEST_CASE("a member keeps the alternative its initializer holds") {
  const auto decoded = scrydantic::decode<Defaults>("{}");
  REQUIRE(decoded);
  CHECK(encoded(*decoded) ==
        R"({"id":7,"items":[{"count":1,"name":"a"}],"name":"anonymous","note":null,)"
        R"("parent":null,"shape":{"radius":2,"type":"circle"}})");
}

// ---- Recursion ---------------------------------------------------------------

TEST_CASE("a model that contains itself through an alternative round trips") {
  const std::string text =
      R"({"value":[{"value":"leaf"},{"value":[{"value":"deep"},{"value":[]}]}]})";
  const auto tree = scrydantic::decode<kind_variants::Tree>(text);
  REQUIRE(tree);
  CHECK(encoded(*tree) == text);
  CHECK(rejection<kind_variants::Tree>(R"({"value":[{"value":"leaf"},{"value":7}]})") ==
        Lines{"wrong_type $.value[1].value must be a string or an array"});

  const std::string formula = R"({"root":{"terms":[1,{"of":[2.5],"type":"negation"},)"
                              R"({"terms":[],"type":"sum"}],"type":"sum"}})";
  const auto decoded = scrydantic::decode<kind_variants::Formula>(formula);
  REQUIRE(decoded);
  CHECK(encoded(*decoded) == formula);
  CHECK(rejection<kind_variants::Formula>(
            R"({"root":{"terms":[true,{"type":"product"}],"type":"sum"}})") ==
        Lines{"wrong_type $.root.terms[0] must be a number or an object",
              "unknown_tag $.root.terms[1].type must be one of: sum, negation"});
}

// ---- Encoding ----------------------------------------------------------------

TEST_CASE("encoding reports a valueless kind-variant as it does a tagged union") {
  struct ThrowingText {
    // NOLINTNEXTLINE(google-explicit-constructor): the conversion is the point.
    operator std::string() const { throw 1; }
  };
  std::vector<OneOrMany> items{OneOrMany{}, OneOrMany{std::vector<Item>{}}};
  try {
    items[1].emplace<Item>(ThrowingText{});
  } catch (const int) {
  }
  REQUIRE(items[1].valueless_by_exception());

  const auto failed = scrydantic::encode(items);
  REQUIRE_FALSE(failed);
  CHECK(failed.error().message == "$[1] is a variant left valueless by an exception");
}

TEST_CASE("an encode failure inside an alternative is reported at its path") {
  const auto failed = scrydantic::encode(
      OneOrMany{std::vector<Item>{{.name = "ok"}, {.name = "\xff"}}});
  REQUIRE_FALSE(failed);
  CHECK(failed.error().message ==
        "the value holds a string that is not valid UTF-8, so it cannot be JSON");
  const auto infinite =
      scrydantic::encode(std::vector<Count>{Count{1}, Count{1.0 / 0.0}});
  REQUIRE_FALSE(infinite);
  CHECK(infinite.error().message == "$[1] must be finite");
}

TEST_CASE("encoding reads a borrowed string alternative") {
  CHECK(encoded(Borrowed{std::string_view{"lent"}}) == R"("lent")");
  CHECK(encoded(Borrowed{3}) == "3");
}

// ---- Strings that stand for another value ------------------------------------

TEST_CASE("a string is the string alternative, whatever the options") {
  const DecodeOptions all{.coerce_scalars = true, .coerce_structures = true};
  CHECK(selected<Id>(R"("7")", all) == 1);
  CHECK(selected<NamedShape>(R"("{\"type\":\"circle\"}")", all) == 0);
  // An enum is a string alternative too, and holds the string to its names.
  CHECK(rejection<Every>(R"("3")", all) ==
        Lines{"unknown_enumerator $ is not a declared enumerator; must be one of: "
              "fast, careful"});
}

TEST_CASE("a variant with no string alternative takes a string for the value it "
          "spells") {
  const DecodeOptions scalars{.coerce_scalars = true};
  const DecodeOptions structures{.coerce_structures = true};

  // The spelled value selects the alternative as if it stood in the string's
  // place.
  CHECK(selected<Count>(R"("5")", scalars) == 0);
  CHECK(selected<Count>(R"("5.5")", scalars) == 1);
  CHECK(selected<Count>(R"("2147483648")", scalars) == 1);
  CHECK(selected<Verbosity>(R"("true")", scalars) == 0);
  CHECK(selected<OneOrMany>(R"("[{\"name\":\"a\"}]")", structures) == 1);
  CHECK(selected<OneOrMany>(R"(" {\"name\":\"a\"} ")", structures) == 0);
  CHECK(selected<Shape>(R"("{\"radius\":1,\"type\":\"circle\"}")", structures) == 0);
  CHECK(selected<Verbosity>(R"("{\"type\":\"loud\"}")", structures) == 1);

  // Each option coerces only the kinds it names, and neither is on by default.
  CHECK(rejection<Count>(R"("5")") ==
        Lines{"wrong_type $ must be an integer or a number"});
  CHECK(rejection<Count>(R"("5")", structures) ==
        Lines{"wrong_type $ must be an integer or a number"});
  CHECK(rejection<Verbosity>(R"("true")", structures) ==
        Lines{"wrong_type $ must be a boolean or an object"});
  CHECK(rejection<OneOrMany>(R"("[]")", scalars) ==
        Lines{"wrong_type $ must be an object or an array"});

  // Text that spells nothing an alternative could be leaves the string as it
  // is, which no alternative takes.
  CHECK(rejection<Count>(R"("five")", scalars) ==
        Lines{"wrong_type $ must be an integer or a number"});
  CHECK(rejection<Count>(R"("true")", scalars) ==
        Lines{"wrong_type $ must be an integer or a number"});
  CHECK(rejection<OneOrMany>(R"("7")", structures) ==
        Lines{"wrong_type $ must be an object or an array"});
  // What it spells is read as its alternative is, at the string's own path.
  CHECK(rejection<std::vector<OneOrMany>>(R"(["[{\"name\":1}]"])", structures) ==
        Lines{"wrong_type $[0][0].name must be a string"});
  CHECK(rejection<std::variant<std::int32_t, bool>>(R"("2147483648")", scalars) ==
        Lines{"out_of_range $ is outside the integer range"});
}

// ---- Tools -------------------------------------------------------------------

TEST_CASE("a tool takes a kind-variant as a parameter and returns one") {
  const auto tools = scrydantic::bind<^^kind_variant_tools>();
  REQUIRE(tools.size() == 2);

  CHECK(tools[0].name == "next");
  CHECK(tools[0].input_schema.text ==
        R"({"additionalProperties":false,"properties":{"id":)" +
            std::string{scrydantic::schema_v<Id>} +
            R"(},"required":["id"],"type":"object"})");
  REQUIRE(tools[0].output_schema.has_value());
  CHECK(tools[0].output_schema->text == scrydantic::schema_v<Id>);
  const auto number = tools[0].invoke(Json{.text = R"({"id":41})"});
  REQUIRE(number);
  CHECK(number->text == "42");
  const auto text = tools[0].invoke(Json{.text = R"({"id":"a"})"});
  REQUIRE(text);
  CHECK(text->text == R"("a+")");
  const auto refused = tools[0].invoke(Json{.text = R"({"id":true})"});
  REQUIRE_FALSE(refused);
  CHECK(refused.error().model_message == "$.id must be an integer or a string");

  CHECK(tools[1].name == "total");
  REQUIRE(tools[1].output_schema.has_value());
  CHECK(tools[1].output_schema->text == scrydantic::schema_v<Count>);
  const auto one =
      tools[1].invoke(Json{.text = R"({"items":{"name":"a"},"owner":null})"});
  REQUIRE(one);
  CHECK(one->text == "1");
  const auto many = tools[1].invoke(
      Json{.text = R"({"items":[{"name":"a"},{"name":"b"}],"owner":3})"});
  REQUIRE(many);
  CHECK(many->text == "2");
  const auto half = tools[1].invoke(Json{.text = R"({"items":[],"owner":"me"})"});
  REQUIRE(half);
  CHECK(half->text == "0.5");
  const auto wrong = tools[1].invoke(Json{.text = R"({"items":"a","owner":[]})"});
  REQUIRE_FALSE(wrong);
  CHECK(wrong.error().model_message == "$.items must be an object or an array\n"
                                       "$.owner must be an integer or a string");
}

TEST_CASE("a typed handler takes an aggregate that holds a kind-variant") {
  const auto echo = scrydantic::bind_handler<Request>(
      [](Request request) { return std::move(request.id); });
  const auto result = echo(Json{.text = R"({"id":"abc"})"});
  REQUIRE(result);
  CHECK(result->text == R"("abc")");
}

} // namespace
