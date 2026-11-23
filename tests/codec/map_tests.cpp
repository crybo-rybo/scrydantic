// String-keyed maps: std::map and std::unordered_map of std::string are the JSON
// object whose keys are data. Their schema, the canonical order they are written
// in whatever order they iterate in, how an entry's key enters a path, recursion
// through a map, and every map the compiler rejects, with its diagnostic.

#include <algorithm>
#include <catch2/catch_test_macros.hpp>
#include <cctype>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <functional>
#include <limits>
#include <map>
#include <optional>
#include <scrydantic/scrydantic.hpp>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <variant>
#include <vector>

namespace {

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

struct Scores {
  std::map<std::string, int> by_player;

  bool operator==(const Scores&) const = default;
};

struct Player {
  std::string team;
  std::int32_t wins{};

  bool operator==(const Player&) const = default;
};

// A comparator of the program's own: keys that differ only in case are one key.
struct IgnoringCase {
  bool operator()(const std::string& left, const std::string& right) const {
    return std::ranges::lexicographical_compare(
        left, right, [](const unsigned char first, const unsigned char second) {
          return std::tolower(first) < std::tolower(second);
        });
  }
};

using Descending = std::map<std::string, int, std::greater<>>;
using Transparent = std::map<std::string, int, std::less<>>;
using Hashed = std::unordered_map<std::string, int>;
using Folded = std::map<std::string, int, IgnoringCase>;

// Every place a map can stand in a model.
struct Places {
  [[= scrydantic::description{"Players by name"}]] std::map<std::string, Player>
      players;
  std::optional<std::map<std::string, bool>> flags;
  std::vector<std::map<std::string, std::string>> rows;
  std::map<std::string, std::vector<std::int32_t>> series;
  std::map<std::string, std::optional<double>> readings;
  Hashed counts{};

  bool operator==(const Places&) const = default;
};

struct[[= scrydantic::skip_null]] Labelled {
  std::optional<std::map<std::string, std::string>> labels;
};

struct Payloads {
  std::map<std::string, Json> by_name;
};

struct Ratios {
  std::map<std::string, double> by_name;
};

struct[[= scrydantic::tag{"circle"}]] Circle {
  double radius{};
};

struct[[= scrydantic::tag{"square"}]] Square {
  double side{};
};

scrydantic::Status holds_a_default(const std::map<std::string, std::string>& settings) {
  return settings.contains("default")
             ? scrydantic::Status{}
             : std::unexpected(scrydantic::rejection("must have a default entry"));
}

struct Settings {
  [[= scrydantic::check<&holds_a_default>]] std::map<std::string, std::string> values;
};

template <typename Type>
[[nodiscard]] scrydantic::Result<Type> decode_text(const std::string_view text) {
  return scrydantic::decode<Type>(text);
}

} // namespace

namespace maps {

// A model that contains itself through a map, as one does through a vector.
struct Folder {
  std::map<std::string, Folder> children;
  std::int32_t size{};

  bool operator==(const Folder&) const = default;
};

struct Drive {
  std::string label;
  Folder root;
};

struct Index {
  std::unordered_map<std::string, Index> entries;
};

// A recursive class template names its definition after its arguments, a map
// among them.
template <typename Value> struct Tree {
  Value value{};
  std::vector<Tree> branches;
};

struct Forest {
  Tree<std::map<std::string, std::int32_t>> ordered;
  Tree<std::unordered_map<std::string, bool>> hashed;
};

// A map's comparator is no part of a definition name, so it has none.
struct Thicket {
  Tree<std::map<std::string, std::int32_t, std::greater<>>> descending;
};

} // namespace maps

namespace map_tools {

struct Tally {
  std::map<std::string, std::int32_t> counts;
};

[[= scrydantic::tool{"Count each word"}]] inline std::map<std::string, std::int32_t>
    count(std::vector<std::string> words) {
  std::map<std::string, std::int32_t> counts{};
  for (const auto& word : words) {
    ++counts[word];
  }
  return counts;
}

[[= scrydantic::tool{"Add up the counts"}]] inline std::int32_t
    total(std::map<std::string, std::int32_t> counts) {
  std::int32_t sum = 0;
  for (const auto& entry : counts) {
    sum += entry.second;
  }
  return sum;
}

using Names = std::unordered_map<std::string, std::string>;

// A map beside another parameter, and an unordered one in a Result.
[[= scrydantic::tool{"Add a suffix to every name"}]] inline scrydantic::Result<Names>
    suffix(Names names, std::string with) {
  for (auto& entry : names) {
    entry.second += with;
  }
  return names;
}

} // namespace map_tools

namespace {

// ---- Families ----------------------------------------------------------------

static_assert(scrydantic::SupportedValue<std::map<std::string, int>>);
static_assert(scrydantic::Encodable<std::map<std::string, int>>);
static_assert(scrydantic::Decodable<std::map<std::string, int>>);
static_assert(scrydantic::SupportedValue<Hashed>);
static_assert(scrydantic::Encodable<Hashed>);
static_assert(scrydantic::Decodable<Hashed>);
static_assert(scrydantic::SupportedValue<Descending>);
static_assert(scrydantic::SupportedValue<Transparent>);
static_assert(scrydantic::SupportedValue<Folded>);
static_assert(scrydantic::SupportedValue<Scores>);
static_assert(scrydantic::SupportedValue<Places>);
static_assert(scrydantic::SupportedValue<maps::Folder>);
static_assert(scrydantic::Decodable<maps::Index>);

// A map is in each family that its mapped type is in, and in no other.
static_assert(scrydantic::SupportedValue<std::map<std::string, Json>> ==
              scrydantic::SupportedValue<Json>);
static_assert(scrydantic::SupportedValue<Payloads> == scrydantic::SupportedValue<Json>);
static_assert(scrydantic::Encodable<Payloads>);
static_assert(scrydantic::Decodable<Payloads>);
static_assert(scrydantic::Encodable<std::map<std::string, std::string_view>>);
static_assert(!scrydantic::Decodable<std::map<std::string, std::string_view>>);
static_assert(!scrydantic::SupportedValue<std::map<std::string, std::string_view>>);

// ---- Schemas -----------------------------------------------------------------

static_assert(
    scrydantic::schema_v<Scores> ==
    R"({"additionalProperties":false,"properties":{"by_player":{"additionalProperties":{"maximum":2147483647,"minimum":-2147483648,"type":"integer"},"type":"object"}},"required":["by_player"],"type":"object"})");

static_assert(scrydantic::schema_v<std::map<std::string, bool>> ==
              R"({"additionalProperties":{"type":"boolean"},"type":"object"})");

// The comparator, the hash, and the container change nothing in the schema.
static_assert(scrydantic::schema_v<Hashed> ==
              scrydantic::schema_v<std::map<std::string, int>>);
static_assert(scrydantic::schema_v<Descending> == scrydantic::schema_v<Hashed>);
static_assert(scrydantic::schema_v<Transparent> == scrydantic::schema_v<Hashed>);
static_assert(scrydantic::schema_v<Folded> == scrydantic::schema_v<Hashed>);

// A description follows "additionalProperties", in key order, and the mapped
// type's schema is written whole: an aggregate's, an optional's, an array's.
static_assert(
    scrydantic::schema_v<Places> ==
    R"({"additionalProperties":false,"properties":{)"
    R"("counts":{"additionalProperties":{"maximum":2147483647,"minimum":-2147483648,"type":"integer"},"type":"object"},)"
    R"("flags":{"anyOf":[{"additionalProperties":{"type":"boolean"},"type":"object"},{"type":"null"}]},)"
    R"("players":{"additionalProperties":{"additionalProperties":false,"properties":{"team":{"type":"string"},"wins":{"maximum":2147483647,"minimum":-2147483648,"type":"integer"}},"required":["team"],"type":"object"},"description":"Players by name","type":"object"},)"
    R"("readings":{"additionalProperties":{"anyOf":[{"type":"number"},{"type":"null"}]},"type":"object"},)"
    R"("rows":{"items":{"additionalProperties":{"type":"string"},"type":"object"},"type":"array"},)"
    R"("series":{"additionalProperties":{"items":{"maximum":2147483647,"minimum":-2147483648,"type":"integer"},"type":"array"},"type":"object"})"
    R"(},"required":["flags","players","readings","rows","series"],"type":"object"})");

static_assert(
    scrydantic::schema_v<std::map<std::string, std::variant<Circle, Square>>> ==
    R"({"additionalProperties":{"anyOf":[{"additionalProperties":false,"properties":{"radius":{"type":"number"},"type":{"enum":["circle"],"type":"string"}},"required":["type"],"type":"object"},{"additionalProperties":false,"properties":{"side":{"type":"number"},"type":{"enum":["square"],"type":"string"}},"required":["type"],"type":"object"}]},"type":"object"})");

// A recursive root refers to itself through the map, as it does through a vector.
static_assert(
    scrydantic::schema_v<maps::Folder> ==
    R"({"additionalProperties":false,"properties":{"children":{"additionalProperties":{"$ref":"#"},"type":"object"},"size":{"maximum":2147483647,"minimum":-2147483648,"type":"integer"}},"required":["children"],"type":"object"})");

// Below a root that is not recursive, the same type is a definition.
static_assert(
    scrydantic::schema_v<maps::Drive> ==
    R"({"$defs":{"maps.Folder":{"additionalProperties":false,"properties":{"children":{"additionalProperties":{"$ref":"#/$defs/maps.Folder"},"type":"object"},"size":{"maximum":2147483647,"minimum":-2147483648,"type":"integer"}},"required":["children"],"type":"object"}},"additionalProperties":false,"properties":{"label":{"type":"string"},"root":{"$ref":"#/$defs/maps.Folder"}},"required":["label","root"],"type":"object"})");

static_assert(
    scrydantic::schema_v<std::map<std::string, maps::Folder>> ==
    R"({"$defs":{"maps.Folder":{"additionalProperties":false,"properties":{"children":{"additionalProperties":{"$ref":"#/$defs/maps.Folder"},"type":"object"},"size":{"maximum":2147483647,"minimum":-2147483648,"type":"integer"}},"required":["children"],"type":"object"}},"additionalProperties":{"$ref":"#/$defs/maps.Folder"},"type":"object"})");

static_assert(
    scrydantic::schema_v<maps::Index> ==
    R"({"additionalProperties":false,"properties":{"entries":{"additionalProperties":{"$ref":"#"},"type":"object"}},"required":["entries"],"type":"object"})");

static_assert(mentions(scrydantic::schema_v<maps::Forest>,
                       R"("$ref":"#/$defs/maps.Tree-map-string-int32")"));
static_assert(mentions(scrydantic::schema_v<maps::Forest>,
                       R"("$ref":"#/$defs/maps.Tree-unordered_map-string-bool")"));

// ---- Rejections --------------------------------------------------------------

struct ByNumber {
  std::map<int, std::string> names;
};

struct ByView {
  std::unordered_map<std::string_view, int> counts;
};

struct Repeated {
  std::multimap<std::string, int> values;
};

struct RepeatedHashed {
  std::unordered_multimap<std::string, int> values;
};

struct Pointed {
  std::map<std::string, std::vector<int*>> addresses;
};

struct Frozen {
  std::map<std::string, const int> limits;
};

struct Bounded {
  [[= scrydantic::items{.max = 4}]] std::map<std::string, int> values;
};

struct Lengthy {
  [[= scrydantic::length{.max = 4}]] std::optional<Hashed> values;
};

struct Skipped {
  [[= scrydantic::skip_null]] std::map<std::string, int> values;
};

static_assert(mentions(scrydantic::supported_value_diagnostic<ByNumber>(),
                       "ByNumber::names: std::map<int, "));
static_assert(mentions(scrydantic::supported_value_diagnostic<ByNumber>(),
                       "> has a key type other than std::string; a JSON object's keys "
                       "are strings"));
static_assert(mentions(scrydantic::encodable_diagnostic<ByView>(),
                       "> has a key type other than std::string; a JSON object's keys "
                       "are strings"));
static_assert(mentions(scrydantic::decodable_diagnostic<std::map<int, int>>(),
                       "std::map<int, int> has a key type other than std::string"));
static_assert(mentions(scrydantic::supported_value_diagnostic<Repeated>(),
                       "Repeated::values: std::multimap<"));
static_assert(mentions(scrydantic::supported_value_diagnostic<Repeated>(),
                       "> can hold one key more than once, which a JSON object "
                       "cannot; use std::map or std::unordered_map"));
static_assert(mentions(scrydantic::encodable_diagnostic<RepeatedHashed>(),
                       "RepeatedHashed::values: std::unordered_multimap<"));
static_assert(mentions(scrydantic::encodable_diagnostic<RepeatedHashed>(),
                       "> can hold one key more than once"));
// The value under any key is `{}`, as any element of an array is `[]`.
static_assert(mentions(scrydantic::supported_value_diagnostic<Pointed>(),
                       "Pointed::addresses{}[]: int* is not a supported reflected "
                       "value"));
static_assert(mentions(scrydantic::supported_value_diagnostic<Frozen>(),
                       "> has a cv-qualified or reference mapped type"));
static_assert(mentions(scrydantic::supported_value_diagnostic<Bounded>(),
                       "Bounded::values: carries scrydantic::items, which applies to "
                       "std::vector members, not to std::map<"));
static_assert(mentions(scrydantic::decodable_diagnostic<Lengthy>(),
                       "Lengthy::values: carries scrydantic::length, which applies to "
                       "std::string members, not to std::optional<"));
static_assert(mentions(scrydantic::supported_value_diagnostic<Skipped>(),
                       "Skipped::values: is not a std::optional, so skip_null and "
                       "emit_null do not apply to it"));
static_assert(
    mentions(scrydantic::supported_value_diagnostic<maps::Thicket>(),
             "is not a type that a definition name spells; an argument type must be"));

// An argument object stays an aggregate, whose members name the arguments.
static_assert(!scrydantic::ToolArguments<std::map<std::string, int>>);
static_assert(mentions(scrydantic::tool_arguments_diagnostic<Hashed>(),
                       "> is a map, whose keys are data; tool arguments are a plain "
                       "aggregate, whose members name the arguments"));
static_assert(scrydantic::ToolArguments<Scores>);
static_assert(scrydantic::input_schema_v<Scores> == scrydantic::schema_v<Scores>);

// ---- Encoding and decoding ---------------------------------------------------

TEST_CASE("a map encodes with its keys in canonical order and decodes back") {
  const Scores scores{.by_player = {{"zed", 1}, {"amy", 2}}};
  const auto encoded = scrydantic::encode(scores);
  REQUIRE(encoded);
  CHECK(encoded->text == R"({"by_player":{"amy":2,"zed":1}})");
  CHECK(decode_text<Scores>(encoded->text) == scores);
  CHECK(scrydantic::encode_verbatim(scores)->text == encoded->text);
}

TEST_CASE("an empty map is the empty object") {
  CHECK(scrydantic::encode(Scores{})->text == R"({"by_player":{}})");
  const auto decoded = decode_text<Scores>(R"({"by_player":{}})");
  REQUIRE(decoded);
  CHECK(decoded->by_player.empty());
  CHECK(scrydantic::encode(Hashed{})->text == "{}");
  CHECK(decode_text<Hashed>("{}")->empty());
}

TEST_CASE("a map is written in canonical order however it iterates") {
  // Byte order: upper case before lower, and both before a two-byte character.
  const std::vector<std::pair<std::string, int>> entries{
      {"zed", 1}, {"Zed", 2}, {"\xC3\xA9t\xC3\xA9", 3},
      {"", 4},    {"amy", 5}, {"am", 6},
      {"a b", 7}, {"10", 8},  {"9", 9}};
  const std::string canonical =
      "{\"\":4,\"10\":8,\"9\":9,\"Zed\":2,\"a b\":7,\"am\":6,\"amy\":5,\"zed\":1,"
      "\"\xC3\xA9t\xC3\xA9\":3}";
  const auto check_order = [&]<typename Map>(const Map& map) {
    const auto encoded = scrydantic::encode(map);
    REQUIRE(encoded);
    CHECK(encoded->text == canonical);
    CHECK(scrydantic::encode_verbatim(map)->text == canonical);
    // The order is the one the JSON core keeps and visits an object's keys in.
    CHECK(scrydantic::JsonView::parse(encoded->text)->to_json().text == canonical);
    const auto decoded = decode_text<Map>(canonical);
    REQUIRE(decoded);
    CHECK(*decoded == map);
  };
  check_order(std::map<std::string, int>{entries.begin(), entries.end()});
  check_order(Transparent{entries.begin(), entries.end()});
  check_order(Descending{entries.begin(), entries.end()});
  check_order(Hashed{entries.begin(), entries.end()});

  // Enough entries that a hash table's order is none an input could have had.
  Hashed many{};
  std::string expected{"{"};
  for (int index = 100; index < 400; ++index) {
    many.emplace("k" + std::to_string(index), index);
    expected += (index == 100 ? "\"k" : ",\"k") + std::to_string(index) +
                "\":" + std::to_string(index);
  }
  expected.push_back('}');
  CHECK(scrydantic::encode(many)->text == expected);
  CHECK(decode_text<Hashed>(expected) == many);
}

TEST_CASE("an issue under a map entry has the key as a path segment") {
  const auto failed = decode_text<Scores>(R"({"by_player":{"amy":"two"}})");
  REQUIRE_FALSE(failed);
  CHECK(failed.error().message == "$.by_player.amy must be an integer");
  CHECK(failed.error().model_message == "$.by_player.amy must be an integer");
  REQUIRE(failed.error().issues.size() == 1);
  CHECK(failed.error().issues[0] == scrydantic::Issue{.kind = IssueKind::wrong_type,
                                                      .path = {"by_player", "amy"},
                                                      .reason = "must be an integer"});
}

TEST_CASE("a key that is not an identifier is quoted in a path, an empty one too") {
  const auto failed = decode_text<Scores>(
      R"({"by_player":{"":null,"a b":1.5,"content-type":"x","q\"uote":[],"ok":1}})");
  REQUIRE_FALSE(failed);
  CHECK(failed.error().model_message ==
        "$.by_player[\"\"] must be an integer\n"
        "$.by_player[\"a b\"] must be an integer\n"
        "$.by_player[\"content-type\"] must be an integer\n"
        "$.by_player[\"q\\\"uote\"] must be an integer");
  REQUIRE(failed.error().issues.size() == 4);
  CHECK(failed.error().issues[0].path ==
        std::vector<scrydantic::PathSegment>{"by_player", ""});

  const auto root = decode_text<Hashed>(R"({"":true})");
  REQUIRE_FALSE(root);
  CHECK(root.error().issues[0].path == std::vector<scrydantic::PathSegment>{""});
  CHECK(root.error().model_message == "$[\"\"] must be an integer");
}

TEST_CASE("a map decodes from an object alone, and reports every entry that fails") {
  for (const std::string_view wrong : {"[]", "null", "\"amy\"", "1"}) {
    INFO(wrong);
    const auto failed =
        decode_text<Scores>(R"({"by_player":)" + std::string{wrong} + "}");
    REQUIRE_FALSE(failed);
    CHECK(failed.error().model_message == "$.by_player must be an object");
    CHECK(failed.error().issues[0].kind == IssueKind::wrong_type);
  }
  // Entries are read in key order, whatever order the text holds them in.
  const auto failed = decode_text<std::map<std::string, std::uint16_t>>(
      R"({"zed":-1,"amy":70000,"bob":3,"cat":"x"})");
  REQUIRE_FALSE(failed);
  CHECK(failed.error().model_message == "$.amy is outside the integer range\n"
                                        "$.cat must be an integer\n"
                                        "$.zed is outside the integer range");
}

TEST_CASE("the last of an input's members of one key is the map's entry") {
  const auto decoded = decode_text<Scores>(R"({"by_player":{"amy":1,"amy":2}})");
  REQUIRE(decoded);
  CHECK(decoded->by_player == std::map<std::string, int>{{"amy", 2}});
  // The earlier member is not read, so it is not checked either.
  CHECK(decode_text<Hashed>(R"({"amy":"one","amy":2})") == Hashed{{"amy", 2}});
}

TEST_CASE("a map with its own comparator keeps the later of the keys it holds equal") {
  const auto decoded = decode_text<Folded>(R"({"amy":1,"Bob":2,"AMY":3,"bob":4})");
  REQUIRE(decoded);
  // In key order the input is AMY, Bob, amy, bob: each first key stays, with the
  // value of the later one.
  REQUIRE(decoded->size() == 2);
  CHECK(decoded->begin()->first == "AMY");
  CHECK(decoded->begin()->second == 1);
  CHECK(decoded->rbegin()->first == "Bob");
  CHECK(decoded->rbegin()->second == 4);
  CHECK(scrydantic::encode(*decoded)->text == R"({"AMY":1,"Bob":4})");
}

TEST_CASE("decoding stops inside a map at the first issue past max_issues") {
  std::string text{"{"};
  for (std::size_t index = 0; index < scrydantic::max_issues + 36; ++index) {
    text.append(index == 0 ? "" : ",")
        .append("\"k" + std::to_string(100 + index) + "\":null");
  }
  text.push_back('}');
  const auto failed = decode_text<Hashed>(text);
  REQUIRE_FALSE(failed);
  REQUIRE(failed.error().issues.size() == scrydantic::max_issues);
  CHECK(failed.error().issues.front().path ==
        std::vector<scrydantic::PathSegment>{"k100"});
  CHECK(failed.error().issues.back().path ==
        std::vector<scrydantic::PathSegment>{"k163"});
  CHECK(failed.error().message.ends_with(
      "\nfurther issues past the first 64 were not reported"));
}

TEST_CASE("a map of aggregates, and a map in an optional and a vector, round trip") {
  const Places places{
      .players = {{"zed", {.team = "red", .wins = 3}}, {"amy", {.team = "blue"}}},
      .flags = std::map<std::string, bool>{{"on", true}, {"off", false}},
      .rows = {{{"b", "2"}, {"a", "1"}}, {}},
      .series = {{"odd", {1, 3}}, {"even", {}}},
      .readings = {{"low", 0.5}, {"gap", std::nullopt}},
      .counts = {{"y", 2}, {"x", 1}},
  };
  const std::string text =
      R"({"counts":{"x":1,"y":2},"flags":{"off":false,"on":true},)"
      R"("players":{"amy":{"team":"blue","wins":0},"zed":{"team":"red","wins":3}},)"
      R"("readings":{"gap":null,"low":0.5},"rows":[{"a":"1","b":"2"},{}],)"
      R"("series":{"even":[],"odd":[1,3]}})";
  CHECK(scrydantic::encode(places)->text == text);
  CHECK(decode_text<Places>(text) == places);

  // A member with a default keeps it when absent, and a null optional has no map.
  const auto sparse = decode_text<Places>(
      R"({"flags":null,"players":{},"readings":{},"rows":[],"series":{}})");
  REQUIRE(sparse);
  CHECK_FALSE(sparse->flags.has_value());
  CHECK(sparse->counts.empty());

  const auto failed = decode_text<Places>(
      R"({"flags":{"on":1},"players":{"amy":{"wins":"3"},"zed":{"team":"red","side":1}},)"
      R"("readings":{"low":"0.5"},"rows":[{},{"a":1}],"series":{"odd":[1,"3"]}})");
  REQUIRE_FALSE(failed);
  CHECK(failed.error().model_message ==
        "$.players.amy.team is a required member\n"
        "$.players.amy.wins must be an integer\n"
        "$.players.zed contains unknown member \"side\"\n"
        "$.flags.on must be a boolean\n"
        "$.rows[1].a must be a string\n"
        "$.series.odd[1] must be an integer\n"
        "$.readings.low must be a number");
}

TEST_CASE("skip_null leaves out an absent map, and reads its absence") {
  CHECK(scrydantic::encode(Labelled{})->text == "{}");
  const Labelled labelled{.labels =
                              std::map<std::string, std::string>{{"env", "prod"}}};
  CHECK(scrydantic::encode(labelled)->text == R"({"labels":{"env":"prod"}})");
  const auto absent = decode_text<Labelled>("{}");
  REQUIRE(absent);
  CHECK_FALSE(absent->labels.has_value());
  const auto present = decode_text<Labelled>(R"({"labels":{}})");
  REQUIRE(present);
  REQUIRE(present->labels.has_value());
  CHECK(present->labels->empty());
}

TEST_CASE("a map of variants decodes each entry by its tag") {
  using Shapes = std::map<std::string, std::variant<Circle, Square>>;
  const auto decoded = decode_text<Shapes>(
      R"({"b":{"side":2,"type":"square"},"a":{"radius":1.5,"type":"circle"}})");
  REQUIRE(decoded);
  CHECK(std::get<Circle>(decoded->at("a")).radius == 1.5);
  CHECK(scrydantic::encode(*decoded)->text ==
        R"({"a":{"radius":1.5,"type":"circle"},"b":{"side":2,"type":"square"}})");
  const auto failed = decode_text<Shapes>(R"({"a":{"type":"hex"},"b":{}})");
  REQUIRE_FALSE(failed);
  CHECK(failed.error().model_message ==
        "$.a.type must be one of: circle, square\n"
        "$.b.type is a required member; must be one of: circle, square");
}

TEST_CASE("a map of Json holds each entry's canonical text, and writes it back") {
  const auto decoded = decode_text<Payloads>(
      R"({"by_name":{"list":[ 1, 2 ],"object":{"b":1,"a":2.50},"nothing":null}})");
  REQUIRE(decoded);
  REQUIRE(decoded->by_name.size() == 3);
  CHECK(decoded->by_name.at("list").text == "[1,2]");
  CHECK(decoded->by_name.at("object").text == R"({"a":2.5,"b":1})");
  CHECK(decoded->by_name.at("nothing").text == "null");
  CHECK(scrydantic::encode(*decoded)->text ==
        R"({"by_name":{"list":[1,2],"nothing":null,"object":{"a":2.5,"b":1}}})");

  // encode() respells a payload and encode_verbatim() copies it, as for a member.
  const Payloads spaced{.by_name = {{"raw", Json{.text = "[ 1.0 ]"}}}};
  CHECK(scrydantic::encode(spaced)->text == R"({"by_name":{"raw":[1]}})");
  CHECK(scrydantic::encode_verbatim(spaced)->text == R"({"by_name":{"raw":[ 1.0 ]}})");

  const auto invalid =
      scrydantic::encode(Payloads{.by_name = {{"a b", Json{.text = "{"}}}});
  REQUIRE_FALSE(invalid);
  CHECK(invalid.error().message == "$.by_name[\"a b\"] is not valid JSON");

  using Root = std::unordered_map<std::string, Json>;
  const auto root = decode_text<Root>(R"({"z":{"k":[]},"a":"text"})");
  REQUIRE(root);
  CHECK(scrydantic::encode(*root)->text == R"({"a":"text","z":{"k":[]}})");
}

TEST_CASE("an encode failure under a map entry is reported at the entry's path") {
  const auto failed = scrydantic::encode(Ratios{
      .by_name = {{"ok", 1.0}, {"per cent", std::numeric_limits<double>::infinity()}}});
  REQUIRE_FALSE(failed);
  CHECK(failed.error().message == "$.by_name[\"per cent\"] must be finite");
  CHECK(failed.error().model_message.empty());
  // Encoding stops at the first failure in the order it writes: key order.
  const auto first = scrydantic::encode(std::unordered_map<std::string, double>{
      {"b", std::numeric_limits<double>::quiet_NaN()},
      {"a", std::numeric_limits<double>::quiet_NaN()}});
  REQUIRE_FALSE(first);
  CHECK(first.error().message == "$.a must be finite");
}

TEST_CASE("a map key that is not UTF-8 fails encoding as such a string value does") {
  const std::string_view expected =
      "the value holds a string that is not valid UTF-8, so it cannot be JSON";
  const auto value = scrydantic::encode(std::vector<std::string>{"\xFF"});
  REQUIRE_FALSE(value);
  CHECK(value.error().message == expected);

  const auto key = scrydantic::encode(Scores{.by_player = {{"amy", 1}, {"\xFF", 2}}});
  REQUIRE_FALSE(key);
  CHECK(key.error().message == expected);
  CHECK(key.error().model_message.empty());
  CHECK(key.error().issues.empty());
  const auto hashed = scrydantic::encode_verbatim(Hashed{{"\xC3", 1}});
  REQUIRE_FALSE(hashed);
  CHECK(hashed.error().message == expected);
}

TEST_CASE("a key is escaped as a string value is") {
  const std::map<std::string, int> keys{{"quote\"", 1}, {"tab\t", 2}, {"back\\", 3}};
  const auto encoded = scrydantic::encode(keys);
  REQUIRE(encoded);
  CHECK(encoded->text == R"({"back\\":3,"quote\"":1,"tab\t":2})");
  CHECK(decode_text<std::map<std::string, int>>(encoded->text) == keys);
}

TEST_CASE("a validator on a map member takes the whole map") {
  CHECK(decode_text<Settings>(R"({"values":{"default":"x"}})"));
  const auto failed = decode_text<Settings>(R"({"values":{"other":"x"}})");
  REQUIRE_FALSE(failed);
  CHECK(failed.error().model_message == "$.values must have a default entry");
  CHECK(failed.error().issues[0].kind == IssueKind::failed_check);
}

// ---- Recursion ---------------------------------------------------------------

TEST_CASE("a model that contains itself through a map round trips") {
  const std::string text =
      R"({"children":{"etc":{"children":{},"size":1},)"
      R"("usr":{"children":{"bin":{"children":{},"size":7}},"size":2}},"size":3})";
  const auto decoded = decode_text<maps::Folder>(text);
  REQUIRE(decoded);
  CHECK(decoded->children.at("usr").children.at("bin").size == 7);
  CHECK(scrydantic::encode(*decoded)->text == text);

  const auto failed = decode_text<maps::Folder>(
      R"({"children":{"usr":{"children":{"bin":{"size":"7"},"lib":7},"size":2}}})");
  REQUIRE_FALSE(failed);
  CHECK(failed.error().model_message ==
        "$.children.usr.children.bin.children is a required member\n"
        "$.children.usr.children.bin.size must be an integer\n"
        "$.children.usr.children.lib must be an object");

  const auto index = decode_text<maps::Index>(R"({"entries":{"a":{"entries":{}}}})");
  REQUIRE(index);
  CHECK(scrydantic::encode(*index)->text == R"({"entries":{"a":{"entries":{}}}})");
}

TEST_CASE("a failed map of a recursive type converts as any failed result does") {
  const auto wrong = decode_text<std::map<std::string, maps::Folder>>("[]");
  REQUIRE_FALSE(wrong);
  CHECK(wrong.error().model_message == "$ must be an object");
  const auto inside = decode_text<std::map<std::string, maps::Folder>>(R"({"a":[]})");
  REQUIRE_FALSE(inside);
  CHECK(inside.error().model_message == "$.a must be an object");
}

// `count` indexes, each in the map of the one before: two containers apiece.
[[nodiscard]] std::string index_text(const std::size_t count) {
  std::string text{};
  for (std::size_t index = 1; index < count; ++index) {
    text += R"({"entries":{"k":)";
  }
  text += R"({"entries":{}})";
  for (std::size_t index = 1; index < count; ++index) {
    text += "}}";
  }
  return text;
}

[[nodiscard]] maps::Index index_of(const std::size_t count) {
  maps::Index index{};
  for (std::size_t made = 1; made < count; ++made) {
    maps::Index outer{};
    outer.entries.emplace("k", std::move(index));
    index = std::move(outer);
  }
  return index;
}

TEST_CASE("maps nest as deep as the parser reads and no deeper") {
  // 128 indexes nest 256 objects, the parser's limit; the innermost is an empty
  // map, which counts as an empty object does.
  const auto deepest = index_text(128);
  const auto decoded = decode_text<maps::Index>(deepest);
  REQUIRE(decoded);
  CHECK(scrydantic::encode(*decoded)->text == deepest);
  CHECK(scrydantic::encode(index_of(128))->text == deepest);

  CHECK_FALSE(decode_text<maps::Index>(index_text(129)));
  std::string path = "$";
  for (std::size_t index = 1; index < 129; ++index) {
    path += ".entries.k";
  }
  const auto encoded = scrydantic::encode(index_of(129));
  REQUIRE_FALSE(encoded);
  CHECK(encoded.error().message ==
        path + " is an array or object nested more than 256 deep, so the value "
               "cannot be JSON");

  // A payload under a key stands inside the map, which is one container more.
  using Level = std::map<std::string, Json>;
  const auto arrays = [](const std::size_t count) {
    return Json{.text = std::string(count, '[') + std::string(count, ']')};
  };
  CHECK(scrydantic::encode(Level{{"k", arrays(255)}}));
  const auto past = scrydantic::encode(Level{{"k", arrays(256)}});
  REQUIRE_FALSE(past);
  CHECK(past.error().message ==
        "the value nests arrays and objects more than 256 deep, so it cannot be JSON");
}

// ---- Tools -------------------------------------------------------------------

TEST_CASE("a tool takes a map as a parameter and returns one as its result") {
  const auto tools = scrydantic::bind<^^map_tools>();
  REQUIRE(tools.size() == 3);

  CHECK(tools[0].name == "count");
  const auto counted =
      tools[0].invoke(Json{.text = R"({"words":["b","a","b","c","b"]})"});
  REQUIRE(counted);
  CHECK(counted->text == R"({"a":1,"b":3,"c":1})");

  CHECK(tools[1].name == "total");
  CHECK(
      tools[1].input_schema.text ==
      R"({"additionalProperties":false,"properties":{"counts":{"additionalProperties":{"maximum":2147483647,"minimum":-2147483648,"type":"integer"},"type":"object"}},"required":["counts"],"type":"object"})");
  const auto summed = tools[1].invoke(Json{.text = R"({"counts":{"a":1,"b":3}})"});
  REQUIRE(summed);
  CHECK(summed->text == "4");
  const auto rejected = tools[1].invoke(Json{.text = R"({"counts":{"a":1,"b":"3"}})"});
  REQUIRE_FALSE(rejected);
  CHECK(rejected.error().model_message == "$.counts.b must be an integer");
  // The map is a member of the argument object, never the object itself.
  const auto bare = tools[1].invoke(Json{.text = R"({"a":1})"});
  REQUIRE_FALSE(bare);
  CHECK(bare.error().model_message == "$ contains unknown member \"a\"\n"
                                      "$.counts is a required member");

  const auto suffixed = tools[2].invoke(
      Json{.text = R"({"names":{"z":"zed","a":"amy","m":"max"},"with":"!"})"});
  REQUIRE(suffixed);
  CHECK(suffixed->text == R"({"a":"amy!","m":"max!","z":"zed!"})");
}

TEST_CASE("a typed handler takes an aggregate that holds a map") {
  const auto counts = scrydantic::bind_handler<map_tools::Tally>(
      [](map_tools::Tally tally) { return std::move(tally.counts); });
  const auto result = counts(Json{.text = R"({"counts":{"b":3,"a":1,"c":2}})"});
  REQUIRE(result);
  CHECK(result->text == R"({"a":1,"b":3,"c":2})");
}

} // namespace
