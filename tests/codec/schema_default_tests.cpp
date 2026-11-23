// The "default" keyword: which members' schemas state one, what it is for each
// kind of value, that it is spelled as encode() spells that value, and the
// defaults a schema leaves out.

#include <array>
#include <catch2/catch_test_macros.hpp>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <memory>
#include <meta>
#include <optional>
#include <scrydantic/annotations.hpp>
#include <scrydantic/codec.hpp>
#include <scrydantic/detail/json_number.hpp>
#include <scrydantic/json.hpp>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace {

// ---- Reading a schema ---------------------------------------------------------

// The end of the JSON string that starts at `begin`, past its closing quote.
constexpr std::size_t string_end(const std::string_view text, const std::size_t begin) {
  std::size_t index = begin + 1;
  while (text[index] != '"') {
    index += text[index] == '\\' ? 2U : 1U;
  }
  return index + 1;
}

// The end of the JSON value that starts at `begin`, in text without whitespace:
// the comma or the closing bracket after it.
constexpr std::size_t value_end(const std::string_view text, const std::size_t begin) {
  std::size_t depth = 0;
  std::size_t index = begin;
  while (index < text.size()) {
    const char character = text[index];
    if (character == '"') {
      index = string_end(text, index);
      continue;
    }
    if (character == '{' || character == '[') {
      ++depth;
    } else if (character == '}' || character == ']') {
      if (depth == 0) {
        return index;
      }
      --depth;
    } else if (character == ',' && depth == 0) {
      return index;
    }
    ++index;
  }
  return index;
}

// The text of the member `key` of `object`, the text of a JSON object without
// whitespace, exactly as it is written there; empty when there is none.
constexpr std::string_view member_text(const std::string_view object,
                                       const std::string_view key) {
  std::size_t index = 1;
  while (index < object.size() && object[index] == '"') {
    const auto key_end = string_end(object, index);
    const auto name = object.substr(index + 1, key_end - index - 2);
    const auto end = value_end(object, key_end + 1);
    if (name == key) {
      return object.substr(key_end + 1, end - key_end - 1);
    }
    index = end + 1;
  }
  return {};
}

// The schema of the property `key` of an object schema.
constexpr std::string_view property(const std::string_view schema,
                                    const std::string_view key) {
  return member_text(member_text(schema, "properties"), key);
}

// The "default" that the schema of Type states for its member `key`, as written.
template <typename Type>
constexpr std::string_view default_of(const std::string_view key) {
  return member_text(property(scrydantic::schema_v<Type>, key), "default");
}

static_assert(member_text(R"({"a":{"b":[1,"}"]},"c":"x,y","d":-1.5})", "a") ==
              R"({"b":[1,"}"]})");
static_assert(member_text(R"({"a":{"b":[1,"}"]},"c":"x,y","d":-1.5})", "c") ==
              R"("x,y")");
static_assert(member_text(R"({"a":{"b":[1,"}"]},"c":"x,y","d":-1.5})", "d") == "-1.5");
static_assert(member_text(R"({"a":{"b":[1,"}"]},"c":"x,y","d":-1.5})", "e").empty());

// ---- The issue's example ------------------------------------------------------

enum class Unit { celsius, fahrenheit };

struct Reading {
  double degrees;
  Unit unit{Unit::celsius};
  std::optional<std::string> label;
};

// A member with an initializer states what it is when absent; one without is
// required, and states nothing.
static_assert(
    scrydantic::schema_v<Reading> ==
    R"({"additionalProperties":false,"properties":{"degrees":{"type":"number"},)"
    R"("label":{"anyOf":[{"type":"string"},{"type":"null"}]},)"
    R"("unit":{"default":"celsius","enum":["celsius","fahrenheit"],"type":"string"}},)"
    R"("required":["degrees","label"],"type":"object"})");

// ---- Every kind of value -------------------------------------------------------

struct Point {
  std::int32_t x{1};
  std::int32_t y{-2};
};

// Its keys are written in their sorted order, a renamed one under its name, and
// a disengaged member that skip_null covers is left out, as encode() leaves it.
struct Label {
  std::string text{"none"};
  [[= scrydantic::name{"colour"}]] std::string color{"red"};
  [[= scrydantic::skip_null]] std::optional<std::int32_t> size{};
  std::optional<std::int32_t> weight{};
};

struct[[= scrydantic::tag{"circle"}]] Circle {
  double radius{1.5};
};

struct[[= scrydantic::tag{"square"}]] Square {
  double side{};
};

// Every member has an initializer that is a constant, and each default is one
// that the schema can spell.
struct Defaults {
  bool flag{true};
  bool off{};
  std::int16_t small{-7};
  std::int64_t lowest{std::numeric_limits<std::int64_t>::min()};
  std::uint64_t highest{std::numeric_limits<std::uint64_t>::max()};
  double ratio{0.1};
  double whole{3.0};
  double tiny{1e-7};
  double huge{-1.5e300};
  double negative_zero{-0.0};
  float half{0.5F};
  float rate{44100.0F};
  float eighth{0.125F};
  float fraction{-0.00390625F};
  float billions{3e9F};
  float ten_billion{1e10F};
  std::string name{"Ada"};
  std::string empty{};
  // A quote, a backslash, the short escapes, a control character that has none,
  // a slash and a DEL, which need none, and text beyond ASCII.
  std::string escaped{"say \"hi\"\\\n\t\b\f\r\x01/\x7F \xC3\xA9\U0001F600"};
  Unit unit{Unit::fahrenheit};
  std::vector<std::int32_t> none{};
  std::vector<std::string> words{"a", "b\"c"};
  std::vector<Unit> units{Unit::celsius, Unit::fahrenheit};
  std::array<bool, 2> pair{true, false};
  std::array<double, 0> nothing{};
  Point origin{};
  Point corner{.x = 640, .y = 480};
  Label label{};
  std::vector<Point> path{Point{}, Point{.x = 3}};
  std::optional<std::int32_t> absent{};
  std::optional<std::int32_t> limit{10};
  std::optional<Point> anchor{Point{}};
  std::indirect<Point> owned{};
  std::variant<Circle, Square> first{};
  std::variant<Circle, Square> second{Square{.side = 2}};
};

static_assert(default_of<Defaults>("flag") == "true");
static_assert(default_of<Defaults>("off") == "false");
static_assert(default_of<Defaults>("small") == "-7");
static_assert(default_of<Defaults>("lowest") == "-9223372036854775808");
static_assert(default_of<Defaults>("highest") == "18446744073709551615");
static_assert(default_of<Defaults>("ratio") == "0.1");
static_assert(default_of<Defaults>("whole") == "3");
static_assert(default_of<Defaults>("tiny") == "1E-7");
static_assert(default_of<Defaults>("huge") == "-1.5E300");
static_assert(default_of<Defaults>("negative_zero") == "-0");
static_assert(default_of<Defaults>("half") == "0.5");
static_assert(default_of<Defaults>("rate") == "44100");
static_assert(default_of<Defaults>("eighth") == "0.125");
static_assert(default_of<Defaults>("fraction") == "-0.00390625");
static_assert(default_of<Defaults>("billions") == "3000000000");
static_assert(default_of<Defaults>("ten_billion") == "10000000000");
static_assert(default_of<Defaults>("name") == R"("Ada")");
static_assert(default_of<Defaults>("empty") == R"("")");
static_assert(default_of<Defaults>("escaped") == R"("say \"hi\"\\\n\t\b\f\r\u0001/)"
                                                 "\x7F \xC3\xA9\U0001F600\"");
static_assert(default_of<Defaults>("unit") == R"("fahrenheit")");
static_assert(default_of<Defaults>("none") == "[]");
static_assert(default_of<Defaults>("words") == R"(["a","b\"c"])");
static_assert(default_of<Defaults>("units") == R"(["celsius","fahrenheit"])");
static_assert(default_of<Defaults>("pair") == "[true,false]");
static_assert(default_of<Defaults>("nothing") == "[]");
static_assert(default_of<Defaults>("origin") == R"({"x":1,"y":-2})");
static_assert(default_of<Defaults>("corner") == R"({"x":640,"y":480})");
static_assert(default_of<Defaults>("label") ==
              R"({"colour":"red","text":"none","weight":null})");
static_assert(default_of<Defaults>("path") == R"([{"x":1,"y":-2},{"x":3,"y":-2}])");
static_assert(default_of<Defaults>("absent") == "null");
static_assert(default_of<Defaults>("limit") == "10");
static_assert(default_of<Defaults>("anchor") == R"({"x":1,"y":-2})");
static_assert(default_of<Defaults>("owned") == R"({"x":1,"y":-2})");
static_assert(default_of<Defaults>("first") == R"({"radius":1.5,"type":"circle"})");
static_assert(default_of<Defaults>("second") == R"({"side":2,"type":"square"})");

// A default stands with the member's other keywords, in sorted order: after
// "anyOf" and "additionalProperties", before "description".
struct Placed {
  [[ = scrydantic::description{"How many"},
     = scrydantic::range{.min = 0} ]] std::int32_t count{5};
  [[= scrydantic::description{"Where"}]] Point at{};
  [[= scrydantic::description{"If any"}]] std::optional<std::int32_t> limit{};
};

static_assert(property(scrydantic::schema_v<Placed>, "count") ==
              R"({"default":5,"description":"How many","maximum":2147483647,)"
              R"("minimum":0,"type":"integer"})");
static_assert(
    property(scrydantic::schema_v<Placed>, "at") ==
    R"({"additionalProperties":false,"default":{"x":1,"y":-2},"description":"Where",)"
    R"("properties":{"x":{"default":1,"maximum":2147483647,"minimum":-2147483648,)"
    R"("type":"integer"},"y":{"default":-2,"maximum":2147483647,)"
    R"("minimum":-2147483648,"type":"integer"}},"required":[],"type":"object"})");
// An optional's default is the member's, which may be null, so it stands beside
// the "anyOf" and not with the engaged value's schema, which admits no null.
static_assert(property(scrydantic::schema_v<Placed>, "limit") ==
              R"({"anyOf":[{"maximum":2147483647,"minimum":-2147483648,)"
              R"("type":"integer"},{"type":"null"}],"default":null,)"
              R"("description":"If any"})");

// ---- An enumerator under another name ------------------------------------------

// The default of an enum member is the name encode() writes for the enumerator,
// whatever decides that name. The tests below hold the schema to that, and to
// the names its own "enum" lists, rather than to a spelling written out here.
enum class Scale {
  celsius[[= scrydantic::name{"degC"}]],
  kelvin,
};

struct Thermostat {
  Scale scale{Scale::celsius};
};

// ---- Recursion -----------------------------------------------------------------

struct Tree {
  std::int32_t value{1};
  std::vector<Tree> children{};
  std::optional<std::indirect<Tree>> parent{};
};

// A recursive root: a default stands beside "$ref" as it does beside "anyOf".
static_assert(scrydantic::schema_v<Tree> ==
              R"({"additionalProperties":false,"properties":{)"
              R"("children":{"default":[],"items":{"$ref":"#"},"type":"array"},)"
              R"("parent":{"anyOf":[{"$ref":"#"},{"type":"null"}],"default":null},)"
              R"("value":{"default":1,"maximum":2147483647,"minimum":-2147483648,)"
              R"("type":"integer"}},"required":[],"type":"object"})");

struct Grove {
  Tree oak{};
  std::vector<Tree> others{Tree{.value = 2}};
};

// A recursive type under "$defs": its members' defaults are in its definition,
// and the default of a member of that type beside the member's "$ref".
static_assert(
    scrydantic::schema_v<Grove> ==
    R"({"$defs":{"anonymous.Tree":{"additionalProperties":false,"properties":{)"
    R"("children":{"default":[],"items":{"$ref":"#/$defs/anonymous.Tree"},"type":"array"},)"
    R"("parent":{"anyOf":[{"$ref":"#/$defs/anonymous.Tree"},{"type":"null"}],"default":null},)"
    R"("value":{"default":1,"maximum":2147483647,"minimum":-2147483648,)"
    R"("type":"integer"}},"required":[],"type":"object"}},)"
    R"("additionalProperties":false,"properties":{)"
    R"("oak":{"$ref":"#/$defs/anonymous.Tree",)"
    R"("default":{"children":[],"parent":null,"value":1}},)"
    R"("others":{"default":[{"children":[],"parent":null,"value":2}],)"
    R"("items":{"$ref":"#/$defs/anonymous.Tree"},"type":"array"}},)"
    R"("required":[],"type":"object"})");

// ---- Members without a default -------------------------------------------------

// Absence decodes as disengaged for a member that skip_null covers, whatever its
// initializer, so neither of these states a default; and a member with no
// initializer is required.
struct[[= scrydantic::skip_null]] Sparse {
  std::optional<std::int32_t> plain;
  std::optional<std::int32_t> initialized{4};
  [[= scrydantic::emit_null]] std::optional<std::int32_t> kept;
  [[= scrydantic::emit_null]] std::optional<std::int32_t> nullable{};
};

static_assert(scrydantic::schema_v<Sparse> ==
              R"({"additionalProperties":false,"properties":{)"
              R"("initialized":{"anyOf":[{"maximum":2147483647,"minimum":-2147483648,)"
              R"("type":"integer"},{"type":"null"}]},)"
              R"("kept":{"anyOf":[{"maximum":2147483647,"minimum":-2147483648,)"
              R"("type":"integer"},{"type":"null"}]},)"
              R"("nullable":{"anyOf":[{"maximum":2147483647,"minimum":-2147483648,)"
              R"("type":"integer"},{"type":"null"}],"default":null},)"
              R"("plain":{"anyOf":[{"maximum":2147483647,"minimum":-2147483648,)"
              R"("type":"integer"},{"type":"null"}]}},)"
              R"("required":["kept"],"type":"object"})");

// A number is left out where the schema could not be sure of spelling it as
// encode() does: a float that is not exactly a decimal of six digits or fewer,
// whose shortest decimal is then not that of the double it is; a double from
// 2^53 up to 2^64, which std::to_chars may write as an integer; and a value
// that is not finite, which encode() refuses.
struct Unspelled {
  float tenth{0.1F};
  float thousandth{0.001F};
  float precise{16777217.0F};
  float subnormal{1e-40F};
  double large{12345678901234567890.0};
  double beyond{1.8446744073709552e19};
  double infinite{std::numeric_limits<double>::infinity()};
  double missing{std::numeric_limits<double>::quiet_NaN()};
  // One member with a default, so that the others are known to lack theirs for
  // their values and not for their class.
  double fine{0.1};
};

static_assert(default_of<Unspelled>("tenth").empty());
static_assert(default_of<Unspelled>("thousandth").empty());
static_assert(default_of<Unspelled>("precise").empty());
static_assert(default_of<Unspelled>("subnormal").empty());
static_assert(default_of<Unspelled>("large").empty());
// From 2^64 up no text fits an integer, so the canonical spelling is encode()'s.
static_assert(default_of<Unspelled>("beyond") == "1.8446744073709552E19");
static_assert(default_of<Unspelled>("infinite").empty());
static_assert(default_of<Unspelled>("missing").empty());
static_assert(default_of<Unspelled>("fine") == "0.1");
static_assert(property(scrydantic::schema_v<Unspelled>, "tenth") ==
              R"({"type":"number"})");

// A value that encode() refuses has no default: an enum value that is no
// enumerator, and a string that is not UTF-8.
struct Refused {
  Unit unit{static_cast<Unit>(7)};
  std::string text{"\xC3"};
  std::vector<std::string> texts{"fine", "\xFF"};
  std::int32_t fine{1};
};

static_assert(default_of<Refused>("unit").empty());
static_assert(default_of<Refused>("text").empty());
static_assert(default_of<Refused>("texts").empty());
static_assert(default_of<Refused>("fine") == "1");

std::string session_id() { return "generated at run time"; }

// An initializer that is not a constant expression: the class still has a
// schema, and decoding still fills the member, but no member's default can be
// read from an object that cannot be built at compile time.
struct Session {
  std::string id{session_id()};
  std::int32_t retries{3};
  Point origin{};
};

static_assert(
    scrydantic::schema_v<Session> ==
    R"({"additionalProperties":false,"properties":{"id":{"type":"string"},)"
    R"("origin":{"additionalProperties":false,"properties":{)"
    R"("x":{"default":1,"maximum":2147483647,"minimum":-2147483648,"type":"integer"},)"
    R"("y":{"default":-2,"maximum":2147483647,"minimum":-2147483648,"type":"integer"}},)"
    R"("required":[],"type":"object"},)"
    R"("retries":{"maximum":2147483647,"minimum":-2147483648,"type":"integer"}},)"
    R"("required":[],"type":"object"})");

// The class is built as decoding builds it, by value-initialization, so a member
// that `{}` cannot initialize, as std::indirect's explicit default constructor
// does not let it, keeps no other member from having a default.
struct Boxed {
  std::indirect<Point> box;
  std::int32_t count{2};
};

static_assert(default_of<Boxed>("box").empty());
static_assert(default_of<Boxed>("count") == "2");

// A default is the program's own value, so it is stated even where the member's
// constraints would reject it from a producer; decoding does not check an
// absent member either.
struct Unchecked {
  [[= scrydantic::range{.min = 1}]] std::int32_t count{};
};

static_assert(default_of<Unchecked>("count") == "0");

// ---- Tests ---------------------------------------------------------------------

// What encode() writes for `value`.
template <typename Type> std::string encoded(const Type& value) {
  const auto json = scrydantic::encode(value);
  REQUIRE(json);
  return json->text;
}

// The canonical spelling of a double, which is all a schema can write.
std::string canonical(const double value) {
  std::string text{};
  scrydantic::detail::append_canonical_double(text, value);
  return text;
}

template <typename Type>
inline constexpr auto members_v =
    std::define_static_array(std::meta::nonstatic_data_members_of(
        ^^Type, std::meta::access_context::unchecked()));

// Checks that the default the schema of Type states for each of its members is
// the text encode() writes for that member of a default-constructed Type, and
// that none is missing.
template <typename Type> void check_defaults_are_encoded() {
  const Type object{};
  template for (constexpr std::meta::info member : members_v<Type>) {
    const std::string key{std::meta::identifier_of(member)};
    CAPTURE(key);
    CHECK(std::string{default_of<Type>(key)} == encoded(object.[:member:]));
  }
}

} // namespace

TEST_CASE("a default is spelled as encode() spells the member's value") {
  check_defaults_are_encoded<Defaults>();
  check_defaults_are_encoded<Point>();
  check_defaults_are_encoded<Tree>();
  check_defaults_are_encoded<Grove>();
  check_defaults_are_encoded<Thermostat>();
}

TEST_CASE("the default of an enum member is the name encode() writes") {
  const auto schema = property(scrydantic::schema_v<Thermostat>, "scale");
  const auto name = encoded(Scale::celsius);
  CHECK(std::string{member_text(schema, "default")} == name);
  // And one of the names the same schema lists.
  CHECK(member_text(schema, "enum").contains(name));
}

TEST_CASE("a schema leaves out the default of a number it could misspell") {
  // The spelling a schema has at compile time is the canonical spelling of the
  // number as a double, which is not what encode() writes for these.
  const Unspelled values{};
  CHECK(encoded(values.tenth) == "0.1");
  CHECK(canonical(static_cast<double>(values.tenth)) == "0.10000000149011612");
  CHECK(encoded(values.large) == "12345678901234567168");
  CHECK(canonical(values.large) == "1.2345678901234567E19");
  // And it is what encode() writes for one that has a default.
  CHECK(encoded(values.beyond) == canonical(values.beyond));
  CHECK(encoded(values.fine) == canonical(values.fine));
}

TEST_CASE("a schema leaves out the default of a value encode() refuses") {
  CHECK_FALSE(scrydantic::encode(Unspelled{}.infinite));
  CHECK_FALSE(scrydantic::encode(Unspelled{}.missing));
  CHECK_FALSE(scrydantic::encode(Refused{}.unit));
  CHECK_FALSE(scrydantic::encode(Refused{}.text));
  CHECK_FALSE(scrydantic::encode(Refused{}.texts));
}

TEST_CASE("an absent member decodes to the default its schema states") {
  const auto reading = scrydantic::decode<Reading>(R"({"degrees":1,"label":null})");
  REQUIRE(reading);
  CHECK(encoded(reading->unit) == default_of<Reading>("unit"));

  const auto defaults = scrydantic::decode<Defaults>("{}");
  REQUIRE(defaults);
  CHECK(encoded(*defaults) == encoded(Defaults{}));

  // A member that skip_null covers decodes as disengaged whatever its
  // initializer, which is why its schema states no default.
  const auto sparse = scrydantic::decode<Sparse>(R"({"kept":null})");
  REQUIRE(sparse);
  CHECK_FALSE(sparse->initialized.has_value());

  // A default outside the member's constraints is still what absence leaves.
  const auto unchecked = scrydantic::decode<Unchecked>("{}");
  REQUIRE(unchecked);
  CHECK(unchecked->count == 0);

  const auto session = scrydantic::decode<Session>("{}");
  REQUIRE(session);
  CHECK(session->id == "generated at run time");
}
