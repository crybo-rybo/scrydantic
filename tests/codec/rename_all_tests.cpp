// scrydantic::rename_all: the keys a class's members and the strings an enum's
// enumerators get from each rule, in schemas, encoding, decoding, keys_v, and
// issue paths; the identifiers a rule reads in a way worth stating; what
// overrides the rule and how far it reaches; and every use of it that the
// compiler rejects, with its diagnostic.

#include <array>
#include <catch2/catch_test_macros.hpp>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <map>
#include <meta>
#include <optional>
#include <scrydantic/annotations.hpp>
#include <scrydantic/codec.hpp>
#include <scrydantic/diagnostics.hpp>
#include <scrydantic/error.hpp>
#include <scrydantic/json.hpp>
#include <scrydantic/tools.hpp>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace naming {

using scrydantic::camel_case;
using scrydantic::IssueKind;
using scrydantic::Json;
using scrydantic::kebab_case;
using scrydantic::name;
using scrydantic::naming_rule;
using scrydantic::pascal_case;
using scrydantic::rename_all;
using scrydantic::screaming_snake_case;

// ---- One class, one rule -------------------------------------------------------

// Multi-word members get their keys from the rule, a one-word member keeps its
// identifier under camel_case, and a name annotation is the key as written.
struct[[= rename_all{camel_case}]] Tool {
  std::string name;
  Json input_schema;
  std::optional<bool> read_only_hint;
  [[= scrydantic::name{"_meta"}]] Json meta;
};

// The same members under each rule.
template <naming_rule Rule> struct[[= rename_all{Rule}]] Limits {
  std::int32_t max_tokens{};
  std::string model;
  std::optional<bool> read_only_hint;

  bool operator==(const Limits&) const = default;
};

static_assert(
    scrydantic::schema_v<Tool> ==
    R"({"additionalProperties":false,"properties":{"_meta":{},"inputSchema":{},"name":{"type":"string"},"readOnlyHint":{"anyOf":[{"type":"boolean"},{"type":"null"}]}},"required":["_meta","inputSchema","name","readOnlyHint"],"type":"object"})");
static_assert(scrydantic::keys_v<Tool> ==
              std::array<std::string_view, 4>{"_meta", "inputSchema", "name",
                                              "readOnlyHint"});

// Keys are sorted as they are written, so a rule that changes a key's first
// letter changes where the member stands.
static_assert(scrydantic::keys_v<Limits<camel_case>> ==
              std::array<std::string_view, 3>{"maxTokens", "model", "readOnlyHint"});
static_assert(scrydantic::keys_v<Limits<pascal_case>> ==
              std::array<std::string_view, 3>{"MaxTokens", "Model", "ReadOnlyHint"});
static_assert(scrydantic::keys_v<Limits<kebab_case>> ==
              std::array<std::string_view, 3>{"max-tokens", "model", "read-only-hint"});
static_assert(scrydantic::keys_v<Limits<screaming_snake_case>> ==
              std::array<std::string_view, 3>{"MAX_TOKENS", "MODEL", "READ_ONLY_HINT"});
static_assert(
    scrydantic::schema_v<Limits<kebab_case>> ==
    R"({"additionalProperties":false,"properties":{"max-tokens":{"default":0,"maximum":2147483647,"minimum":-2147483648,"type":"integer"},"model":{"type":"string"},"read-only-hint":{"anyOf":[{"type":"boolean"},{"type":"null"}]}},"required":["model","read-only-hint"],"type":"object"})");

// ---- Identifiers, one at a time ------------------------------------------------

// The key that a rule derives from one identifier: the only key of a class that
// declares one member of that name. A macro, because the identifier is the
// subject of the test and nothing but a declaration can spell one.
#define SCRYDANTIC_TEST_KEY(rule, identifier)                                          \
  [] {                                                                                 \
    struct[[= scrydantic::rename_all{scrydantic::rule}]] Declares {                    \
      int identifier;                                                                  \
    };                                                                                 \
    return scrydantic::keys_v<Declares>[0];                                            \
  }()

// Words are what underscores separate.
static_assert(SCRYDANTIC_TEST_KEY(camel_case, read_only_hint) == "readOnlyHint");
static_assert(SCRYDANTIC_TEST_KEY(pascal_case, read_only_hint) == "ReadOnlyHint");
static_assert(SCRYDANTIC_TEST_KEY(kebab_case, read_only_hint) == "read-only-hint");
static_assert(SCRYDANTIC_TEST_KEY(screaming_snake_case, read_only_hint) ==
              "READ_ONLY_HINT");
static_assert(SCRYDANTIC_TEST_KEY(camel_case, name) == "name");
static_assert(SCRYDANTIC_TEST_KEY(pascal_case, name) == "Name");
static_assert(SCRYDANTIC_TEST_KEY(kebab_case, name) == "name");
static_assert(SCRYDANTIC_TEST_KEY(screaming_snake_case, name) == "NAME");
static_assert(SCRYDANTIC_TEST_KEY(camel_case, x) == "x");
static_assert(SCRYDANTIC_TEST_KEY(pascal_case, x) == "X");

// A trailing underscore is dropped, which is how a member named around a
// keyword gets the keyword as its key. So are several.
static_assert(SCRYDANTIC_TEST_KEY(camel_case, default_) == "default");
static_assert(SCRYDANTIC_TEST_KEY(pascal_case, default_) == "Default");
static_assert(SCRYDANTIC_TEST_KEY(kebab_case, default_) == "default");
static_assert(SCRYDANTIC_TEST_KEY(screaming_snake_case, default_) == "DEFAULT");
static_assert(SCRYDANTIC_TEST_KEY(camel_case, read_only_) == "readOnly");
static_assert(SCRYDANTIC_TEST_KEY(kebab_case, read_only__) == "read-only");

// Leading underscores are kept, in front of the words; the first word is still
// the first word.
static_assert(SCRYDANTIC_TEST_KEY(camel_case, _meta) == "_meta");
static_assert(SCRYDANTIC_TEST_KEY(pascal_case, _meta) == "_Meta");
static_assert(SCRYDANTIC_TEST_KEY(kebab_case, _meta) == "_meta");
static_assert(SCRYDANTIC_TEST_KEY(screaming_snake_case, _meta) == "_META");
static_assert(SCRYDANTIC_TEST_KEY(camel_case, _meta_data) == "_metaData");
static_assert(SCRYDANTIC_TEST_KEY(kebab_case, _meta_data) == "_meta-data");
static_assert(SCRYDANTIC_TEST_KEY(camel_case, __meta_data_) == "__metaData");
// An identifier with no word in it is kept whole.
static_assert(SCRYDANTIC_TEST_KEY(camel_case, _) == "_");
static_assert(SCRYDANTIC_TEST_KEY(screaming_snake_case, __) == "__");

// Doubled underscores separate two words once, and make no empty word.
static_assert(SCRYDANTIC_TEST_KEY(camel_case, a__b) == "aB");
static_assert(SCRYDANTIC_TEST_KEY(pascal_case, a__b) == "AB");
static_assert(SCRYDANTIC_TEST_KEY(kebab_case, a__b) == "a-b");
static_assert(SCRYDANTIC_TEST_KEY(screaming_snake_case, a___b) == "A_B");

// A digit stays in its word and has no case, so a word that starts with one
// joins the word before it under camel_case and pascal_case.
static_assert(SCRYDANTIC_TEST_KEY(camel_case, http2_enabled) == "http2Enabled");
static_assert(SCRYDANTIC_TEST_KEY(pascal_case, http2_enabled) == "Http2Enabled");
static_assert(SCRYDANTIC_TEST_KEY(kebab_case, http2_enabled) == "http2-enabled");
static_assert(SCRYDANTIC_TEST_KEY(screaming_snake_case, http2_enabled) ==
              "HTTP2_ENABLED");
static_assert(SCRYDANTIC_TEST_KEY(camel_case, sha_256_sum) == "sha256Sum");
static_assert(SCRYDANTIC_TEST_KEY(pascal_case, sha_256) == "Sha256");
static_assert(SCRYDANTIC_TEST_KEY(kebab_case, sha_256) == "sha-256");
static_assert(SCRYDANTIC_TEST_KEY(screaming_snake_case, sha_256) == "SHA_256");

// A capital starts no word. camel_case and pascal_case set a word's first
// letter and keep the rest as written; the other two set every letter.
static_assert(SCRYDANTIC_TEST_KEY(camel_case, userID) == "userID");
static_assert(SCRYDANTIC_TEST_KEY(pascal_case, userID) == "UserID");
static_assert(SCRYDANTIC_TEST_KEY(kebab_case, userID) == "userid");
static_assert(SCRYDANTIC_TEST_KEY(screaming_snake_case, userID) == "USERID");
static_assert(SCRYDANTIC_TEST_KEY(camel_case, HTTPServer) == "hTTPServer");
static_assert(SCRYDANTIC_TEST_KEY(pascal_case, HTTPServer) == "HTTPServer");
static_assert(SCRYDANTIC_TEST_KEY(kebab_case, HTTPServer) == "httpserver");
static_assert(SCRYDANTIC_TEST_KEY(screaming_snake_case, HTTPServer) == "HTTPSERVER");
static_assert(SCRYDANTIC_TEST_KEY(camel_case, Read_Only_URL) == "readOnlyURL");
static_assert(SCRYDANTIC_TEST_KEY(pascal_case, read_onlyURL) == "ReadOnlyURL");
static_assert(SCRYDANTIC_TEST_KEY(kebab_case, Read_Only_URL) == "read-only-url");
static_assert(SCRYDANTIC_TEST_KEY(screaming_snake_case, Read_Only_URL) ==
              "READ_ONLY_URL");

// Only ASCII letters have a case to change.
static_assert(SCRYDANTIC_TEST_KEY(screaming_snake_case, größe_max) == "GRößE_MAX");
static_assert(SCRYDANTIC_TEST_KEY(pascal_case, über_größe) == "überGröße");

#undef SCRYDANTIC_TEST_KEY

// A rule is stated, never implied.
static_assert(!std::default_initializable<rename_all>);
static_assert(!std::convertible_to<naming_rule, rename_all>);

// ---- Enums ---------------------------------------------------------------------

enum class[[= rename_all{screaming_snake_case}]] Level : std::uint8_t {
  low_water,
  high_water,
  off,
};

// A keyword-named enumerator needs no name annotation under a rule, and one
// that carries a name keeps it.
enum class[[= rename_all{kebab_case}]] Access : std::uint8_t {
  public_,
  read_only,
  read_write[[= name{"rw"}]],
};

// Enumerators written in another convention are single words.
enum class[[= rename_all{camel_case}]] Color : std::uint8_t {
  Red,
  DarkBlue,
  dark_green,
};

enum class[[= rename_all{pascal_case}]] Phase : std::uint8_t {
  warm_up,
  steady_state,
};

struct Gauge {
  Level level{};
  std::vector<Access> access;

  bool operator==(const Gauge&) const = default;
};

static_assert(scrydantic::schema_v<Level> ==
              R"({"enum":["LOW_WATER","HIGH_WATER","OFF"],"type":"string"})");
static_assert(scrydantic::schema_v<Access> ==
              R"({"enum":["public","read-only","rw"],"type":"string"})");
static_assert(scrydantic::schema_v<Color> ==
              R"({"enum":["red","darkBlue","darkGreen"],"type":"string"})");
static_assert(scrydantic::schema_v<Phase> ==
              R"({"enum":["WarmUp","SteadyState"],"type":"string"})");

// ---- How far the rule reaches --------------------------------------------------

struct Retry {
  std::int32_t max_attempts{};

  bool operator==(const Retry&) const = default;
};

// A member's type states its own rule, or none, whatever the class that holds
// it says; the keys of a map are data, and no rule's business.
struct[[= rename_all{camel_case}]] Request {
  Limits<kebab_case> rate_limits;
  Retry retry_policy;
  std::vector<Limits<screaming_snake_case>> past_limits;
  Level water_level{};
  std::map<std::string, std::int32_t> word_counts;

  bool operator==(const Request&) const = default;
};

static_assert(scrydantic::keys_v<Request> ==
              std::array<std::string_view, 5>{"pastLimits", "rateLimits", "retryPolicy",
                                              "waterLevel", "wordCounts"});

// The automatic `default` is the encoded initializer, so it is spelled as the
// encoder spells it: each object by its own class's rule, an enumerator by its
// enum's.
struct[[= rename_all{camel_case}]] Settings {
  Level water_level{Level::high_water};
  Limits<kebab_case> rate_limits{.max_tokens = 5, .model = "m", .read_only_hint = {}};
  Retry retry_policy{.max_attempts = 3};
};

static_assert(
    scrydantic::schema_v<Settings> ==
    R"({"additionalProperties":false,"properties":{"rateLimits":{"additionalProperties":false,"default":{"max-tokens":5,"model":"m","read-only-hint":null},"properties":{"max-tokens":{"default":0,"maximum":2147483647,"minimum":-2147483648,"type":"integer"},"model":{"type":"string"},"read-only-hint":{"anyOf":[{"type":"boolean"},{"type":"null"}]}},"required":["model","read-only-hint"],"type":"object"},"retryPolicy":{"additionalProperties":false,"default":{"max_attempts":3},"properties":{"max_attempts":{"default":0,"maximum":2147483647,"minimum":-2147483648,"type":"integer"}},"required":[],"type":"object"},"waterLevel":{"default":"HIGH_WATER","enum":["LOW_WATER","HIGH_WATER","OFF"],"type":"string"}},"required":[],"type":"object"})");

// Class annotations beside the rule read the members it renamed.
struct[[ = rename_all{camel_case}, = scrydantic::skip_null ]] Sparse {
  std::optional<std::int32_t> page_size;
  std::int32_t page_index;
  [[= scrydantic::description{"Rows to leave out"}]] std::int32_t row_offset{};

  bool operator==(const Sparse&) const = default;
};

static_assert(
    scrydantic::schema_v<Sparse> ==
    R"({"additionalProperties":false,"properties":{"pageIndex":{"maximum":2147483647,"minimum":-2147483648,"type":"integer"},"pageSize":{"anyOf":[{"maximum":2147483647,"minimum":-2147483648,"type":"integer"},{"type":"null"}]},"rowOffset":{"default":0,"description":"Rows to leave out","maximum":2147483647,"minimum":-2147483648,"type":"integer"}},"required":["pageIndex"],"type":"object"})");

// A field that reaches an object through a flattened member follows the class
// that declares it: the flattened class's rule, and not the enclosing one's.
struct[[= rename_all{camel_case}]] Page {
  std::optional<std::string> next_cursor;

  bool operator==(const Page&) const = default;
};

struct Listing {
  std::vector<std::string> tool_names;
  [[= scrydantic::flatten]] Page page;

  bool operator==(const Listing&) const = default;
};

struct Audit {
  std::string created_at;

  bool operator==(const Audit&) const = default;
};

struct[[= rename_all{camel_case}]] Document {
  std::string doc_title;
  [[= scrydantic::flatten]] Audit audit_trail;

  bool operator==(const Document&) const = default;
};

static_assert(scrydantic::keys_v<Listing> ==
              std::array<std::string_view, 2>{"nextCursor", "tool_names"});
static_assert(scrydantic::keys_v<Document> ==
              std::array<std::string_view, 2>{"created_at", "docTitle"});

// A variant alternative's members follow its rule. Its tag is written out, and
// stays as written.
struct[[ = scrydantic::tag{"text_block"}, = rename_all{camel_case} ]] TextBlock {
  std::string body_text;

  bool operator==(const TextBlock&) const = default;
};

struct[[= scrydantic::tag{"image_block"}]] ImageBlock {
  std::string image_url;

  bool operator==(const ImageBlock&) const = default;
};

using Block = std::variant<TextBlock, ImageBlock>;

static_assert(scrydantic::tags_v<Block> ==
              std::array<std::string_view, 2>{"text_block", "image_block"});
static_assert(
    scrydantic::schema_v<Block> ==
    R"({"anyOf":[{"additionalProperties":false,"properties":{"bodyText":{"type":"string"},"type":{"enum":["text_block"],"type":"string"}},"required":["bodyText","type"],"type":"object"},{"additionalProperties":false,"properties":{"image_url":{"type":"string"},"type":{"enum":["image_block"],"type":"string"}},"required":["image_url","type"],"type":"object"}]})");

// A class template states one rule for every specialization.
template <typename Value> struct[[= rename_all{pascal_case}]] Tagged {
  Value tagged_value;
};

static_assert(scrydantic::keys_v<Tagged<std::string>> ==
              std::array<std::string_view, 1>{"TaggedValue"});
static_assert(scrydantic::keys_v<Tagged<Retry>> ==
              std::array<std::string_view, 1>{"TaggedValue"});

// ---- Functions -----------------------------------------------------------------

// An argument object synthesized from a parameter list is no class that states
// a rule, so its keys are the parameters' names; a parameter's type keeps its
// own. A lone aggregate parameter is the argument object, rule and all.
struct[[= rename_all{camel_case}]] Search {
  std::string search_text;
  std::int32_t page_size{10};
};

inline std::int32_t resize(std::int32_t max_width, Limits<camel_case> size_limits) {
  return max_width + size_limits.max_tokens;
}

inline std::string search(Search query) { return query.search_text; }

static_assert(scrydantic::keys_v<scrydantic::arguments_t<^^resize>> ==
              std::array<std::string_view, 2>{"max_width", "size_limits"});
static_assert(
    scrydantic::input_schema_v<scrydantic::arguments_t<^^resize>> ==
    R"({"additionalProperties":false,"properties":{"max_width":{"maximum":2147483647,"minimum":-2147483648,"type":"integer"},"size_limits":{"additionalProperties":false,"properties":{"maxTokens":{"default":0,"maximum":2147483647,"minimum":-2147483648,"type":"integer"},"model":{"type":"string"},"readOnlyHint":{"anyOf":[{"type":"boolean"},{"type":"null"}]}},"required":["model","readOnlyHint"],"type":"object"}},"required":["max_width","size_limits"],"type":"object"})");
static_assert(std::same_as<scrydantic::arguments_t<^^search>, Search>);
static_assert(scrydantic::keys_v<scrydantic::arguments_t<^^search>> ==
              std::array<std::string_view, 2>{"pageSize", "searchText"});

// ---- Rejected at compile time --------------------------------------------------

// Two identifiers that the rule spells alike, a derived key that lands on a
// written one, and a derived key that meets a flattened class's.
struct[[= rename_all{camel_case}]] Collides {
  std::int32_t max_tokens{};
  std::int32_t maxTokens{};
};

struct[[= rename_all{kebab_case}]] TrailsAlike {
  std::int32_t value{};
  std::int32_t value_{};
};

struct[[= rename_all{camel_case}]] NamedAlike {
  [[= name{"maxTokens"}]] std::int32_t limit{};
  std::int32_t max_tokens{};
};

struct[[= rename_all{camel_case}]] FlattensAlike {
  std::string next_cursor;
  [[= scrydantic::flatten]] Page page;
};

enum class[[= rename_all{kebab_case}]] CasedAlike : std::uint8_t {
  Low,
  low,
};

enum class[[= rename_all{camel_case}]] NamedAlikeEnum : std::uint8_t {
  read_only,
  locked[[= name{"readOnly"}]],
};

// A second rule, even the same one, is a mistake: one of them says nothing.
struct[[ = rename_all{camel_case}, = rename_all{kebab_case} ]] TwoRules {
  std::int32_t max_tokens{};
};

struct[[ = rename_all{camel_case}, = rename_all{camel_case} ]] SameRuleTwice {
  std::int32_t max_tokens{};
};

enum class[[ = rename_all{camel_case},
             = rename_all{kebab_case} ]] TwoRulesEnum : std::uint8_t {
  first_value,
};

// The rule belongs to what declares the names, not to one of them.
struct OnMember {
  [[= rename_all{camel_case}]] std::int32_t max_tokens{};
};

enum class OnEnumerator : std::uint8_t {
  first_value[[= rename_all{camel_case}]],
};

struct HoldsTwoRules {
  std::vector<TwoRules> rules;
  TwoRulesEnum choice{};
};

// `type_` is the key "type" under a rule, which a variant alternative's tag has.
struct[[ = scrydantic::tag{"typed"}, = rename_all{camel_case} ]] Typed {
  std::string type_;
};

struct Holder {
  std::variant<Typed> held;
};

} // namespace naming

namespace {

using namespace naming;

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

// One check serves the three value families, so each rejects such a type.
static_assert(!scrydantic::SupportedValue<Collides>);
static_assert(!scrydantic::Encodable<Collides>);
static_assert(!scrydantic::Decodable<Collides>);
static_assert(mentions(scrydantic::supported_value_diagnostic<Collides>(),
                       "naming::Collides has members max_tokens and maxTokens that "
                       "both map to the JSON key \"maxTokens\""));
static_assert(mentions(scrydantic::encodable_diagnostic<TrailsAlike>(),
                       "naming::TrailsAlike has members value and value_ that both "
                       "map to the JSON key \"value\""));
static_assert(mentions(scrydantic::decodable_diagnostic<NamedAlike>(),
                       "naming::NamedAlike has members limit and max_tokens that both "
                       "map to the JSON key \"maxTokens\""));
static_assert(mentions(scrydantic::supported_value_diagnostic<FlattensAlike>(),
                       "naming::FlattensAlike has members next_cursor and "
                       "page.next_cursor that both map to the JSON key "
                       "\"nextCursor\""));
static_assert(mentions(scrydantic::supported_value_diagnostic<CasedAlike>(),
                       "naming::CasedAlike has enumerators Low and low that both map "
                       "to the JSON string \"low\""));
static_assert(mentions(scrydantic::decodable_diagnostic<NamedAlikeEnum>(),
                       "naming::NamedAlikeEnum has enumerators read_only and locked "
                       "that both map to the JSON string \"readOnly\""));

static_assert(!scrydantic::SupportedValue<TwoRules>);
static_assert(!scrydantic::Encodable<TwoRules>);
static_assert(!scrydantic::Decodable<TwoRules>);
static_assert(mentions(scrydantic::supported_value_diagnostic<TwoRules>(),
                       "naming::TwoRules has more than one scrydantic::rename_all "
                       "annotation"));
static_assert(mentions(scrydantic::encodable_diagnostic<SameRuleTwice>(),
                       "naming::SameRuleTwice has more than one "
                       "scrydantic::rename_all annotation"));
static_assert(mentions(scrydantic::decodable_diagnostic<TwoRulesEnum>(),
                       "naming::TwoRulesEnum has more than one "
                       "scrydantic::rename_all annotation"));
// Where the type is a member's, the path to the member comes first.
static_assert(mentions(scrydantic::supported_value_diagnostic<HoldsTwoRules>(),
                       "naming::HoldsTwoRules::rules[]: naming::TwoRules has more "
                       "than one scrydantic::rename_all annotation"));

static_assert(mentions(scrydantic::supported_value_diagnostic<OnMember>(),
                       "naming::OnMember::max_tokens: carries scrydantic::rename_all, "
                       "which applies to classes and enums; scrydantic::name gives "
                       "one member its key"));
static_assert(mentions(scrydantic::encodable_diagnostic<OnEnumerator>(),
                       "naming::OnEnumerator::first_value carries "
                       "scrydantic::rename_all, which does not apply to enumerators; "
                       "an enumerator takes only scrydantic::name"));

static_assert(mentions(scrydantic::supported_value_diagnostic<Holder>(),
                       "naming::Holder::held<naming::Typed>.type_: has the JSON key "
                       "\"type\", which carries the variant's tag"));
// Outside a variant the same class has no tag member to meet.
static_assert(scrydantic::keys_v<Typed> == std::array<std::string_view, 1>{"type"});

template <typename Value> [[nodiscard]] std::string encoded(const Value& value) {
  const auto json = scrydantic::encode(value);
  REQUIRE(json);
  return json->text;
}

TEST_CASE("a class's rule names its members in JSON") {
  const Tool tool{.name = "search",
                  .input_schema = Json{.text = R"({"type":"object"})"},
                  .read_only_hint = true,
                  .meta = Json{.text = R"({"origin":"test"})"}};
  const std::string_view text =
      R"({"_meta":{"origin":"test"},"inputSchema":{"type":"object"},"name":"search","readOnlyHint":true})";
  CHECK(encoded(tool) == text);

  const auto decoded = scrydantic::decode<Tool>(text);
  REQUIRE(decoded);
  CHECK(decoded->name == "search");
  CHECK(decoded->input_schema.text == R"({"type":"object"})");
  CHECK(decoded->read_only_hint == true);
  CHECK(decoded->meta.text == R"({"origin":"test"})");

  // The guide's value.
  const Tool guide{
      .name = "search", .input_schema = {"{}"}, .read_only_hint = true, .meta = {"{}"}};
  CHECK(encoded(guide) ==
        R"({"_meta":{},"inputSchema":{},"name":"search","readOnlyHint":true})");
}

TEST_CASE("an identifier that the rule renamed is no key") {
  const auto identifier = scrydantic::decode<Tool>(
      R"({"_meta":{},"input_schema":{},"name":"search","readOnlyHint":true})");
  REQUIRE_FALSE(identifier);
  REQUIRE(identifier.error().issues.size() == 2);
  CHECK(identifier.error().issues[0].kind == IssueKind::unknown_member);
  CHECK(identifier.error().issues[1].kind == IssueKind::missing_member);
  CHECK(identifier.error().model_message ==
        "$ contains unknown member \"input_schema\"\n"
        "$.inputSchema is a required member");

  // Nor is the identifier of a member whose name annotation overrode the rule.
  const auto overridden = scrydantic::decode<Tool>(
      R"({"inputSchema":{},"meta":{},"name":"search","readOnlyHint":null})");
  REQUIRE_FALSE(overridden);
  CHECK(overridden.error().model_message == "$ contains unknown member \"meta\"\n"
                                            "$._meta is a required member");
}

TEST_CASE("each rule round-trips its own keys") {
  const Limits<camel_case> camel{
      .max_tokens = 5, .model = "m", .read_only_hint = false};
  CHECK(encoded(camel) == R"({"maxTokens":5,"model":"m","readOnlyHint":false})");
  CHECK(scrydantic::decode<Limits<camel_case>>(encoded(camel)).value() == camel);

  const Limits<pascal_case> pascal{
      .max_tokens = 5, .model = "m", .read_only_hint = false};
  CHECK(encoded(pascal) == R"({"MaxTokens":5,"Model":"m","ReadOnlyHint":false})");
  CHECK(scrydantic::decode<Limits<pascal_case>>(encoded(pascal)).value() == pascal);

  const Limits<kebab_case> kebab{
      .max_tokens = 5, .model = "m", .read_only_hint = false};
  CHECK(encoded(kebab) == R"({"max-tokens":5,"model":"m","read-only-hint":false})");
  CHECK(scrydantic::decode<Limits<kebab_case>>(encoded(kebab)).value() == kebab);

  const Limits<screaming_snake_case> screaming{
      .max_tokens = 5, .model = "m", .read_only_hint = false};
  CHECK(encoded(screaming) == R"({"MAX_TOKENS":5,"MODEL":"m","READ_ONLY_HINT":false})");
  CHECK(scrydantic::decode<Limits<screaming_snake_case>>(encoded(screaming)).value() ==
        screaming);

  // One rule's keys are another's unknown members.
  const auto crossed = scrydantic::decode<Limits<pascal_case>>(encoded(camel));
  REQUIRE_FALSE(crossed);
  CHECK(crossed.error().issues.front().kind == IssueKind::unknown_member);
}

TEST_CASE("an enum's rule names its enumerators in JSON") {
  const Gauge gauge{.level = Level::high_water,
                    .access = {Access::public_, Access::read_only, Access::read_write}};
  CHECK(encoded(gauge) ==
        R"({"access":["public","read-only","rw"],"level":"HIGH_WATER"})");
  CHECK(scrydantic::decode<Gauge>(encoded(gauge)).value() == gauge);

  CHECK(encoded(Color::Red) == R"("red")");
  CHECK(encoded(Color::DarkBlue) == R"("darkBlue")");
  CHECK(encoded(Color::dark_green) == R"("darkGreen")");
  CHECK(scrydantic::decode<Color>(R"("darkGreen")").value() == Color::dark_green);
  CHECK(encoded(Phase::steady_state) == R"("SteadyState")");
  CHECK(scrydantic::decode<Phase>(R"("WarmUp")").value() == Phase::warm_up);

  // The identifier is not the enumerator's name, and the reason lists the names
  // that are.
  const auto identifier = scrydantic::decode<Gauge>(
      R"({"access":["read_only","read_write"],"level":"high_water"})");
  REQUIRE_FALSE(identifier);
  CHECK(identifier.error().model_message ==
        "$.level is not a declared enumerator; must be one of: LOW_WATER, "
        "HIGH_WATER, OFF\n"
        "$.access[0] is not a declared enumerator; must be one of: public, "
        "read-only, rw\n"
        "$.access[1] is not a declared enumerator; must be one of: public, "
        "read-only, rw");
}

TEST_CASE("a rule stops at the class that states it") {
  const Request request{
      .rate_limits = {.max_tokens = 1, .model = "a", .read_only_hint = {}},
      .retry_policy = {.max_attempts = 2},
      .past_limits = {{.max_tokens = 3, .model = "b", .read_only_hint = true}},
      .water_level = Level::low_water,
      .word_counts = {{"snake_key", 1}, {"camelKey", 2}}};
  const auto text = encoded(request);
  CHECK(
      text ==
      R"({"pastLimits":[{"MAX_TOKENS":3,"MODEL":"b","READ_ONLY_HINT":true}],"rateLimits":{"max-tokens":1,"model":"a","read-only-hint":null},"retryPolicy":{"max_attempts":2},"waterLevel":"LOW_WATER","wordCounts":{"camelKey":2,"snake_key":1}})");
  CHECK(scrydantic::decode<Request>(text).value() == request);
}

TEST_CASE("an issue is reported at the derived keys") {
  const auto rejected = scrydantic::decode<Request>(
      R"({"pastLimits":[{"MAX_TOKENS":"3","MODEL":"b","READ_ONLY_HINT":true}],"rateLimits":{"max-tokens":1,"model":"a"},"retryPolicy":{"maxAttempts":2},"waterLevel":"LOW_WATER","wordCounts":{}})");
  REQUIRE_FALSE(rejected);
  // A key that is no JSONPath name, as kebab_case's are, is spelled in brackets.
  CHECK(rejected.error().model_message ==
        "$.rateLimits[\"read-only-hint\"] is a required member\n"
        "$.retryPolicy contains unknown member \"maxAttempts\"\n"
        "$.pastLimits[0].MAX_TOKENS must be an integer");
}

TEST_CASE("class annotations beside a rule read the renamed members") {
  CHECK(encoded(Sparse{.page_size = {}, .page_index = 2, .row_offset = 4}) ==
        R"({"pageIndex":2,"rowOffset":4})");
  CHECK(scrydantic::decode<Sparse>(R"({"pageIndex":2})").value() ==
        Sparse{.page_size = {}, .page_index = 2, .row_offset = 0});
  const auto absent = scrydantic::decode<Sparse>(R"({"pageSize":20})");
  REQUIRE_FALSE(absent);
  CHECK(absent.error().model_message == "$.pageIndex is a required member");
}

TEST_CASE("a flattened field follows the rule of the class that declares it") {
  const Listing listing{.tool_names = {"search"}, .page = {.next_cursor = "c2"}};
  CHECK(encoded(listing) == R"({"nextCursor":"c2","tool_names":["search"]})");
  CHECK(scrydantic::decode<Listing>(encoded(listing)).value() == listing);
  const auto snake =
      scrydantic::decode<Listing>(R"({"next_cursor":"c2","tool_names":[]})");
  REQUIRE_FALSE(snake);
  CHECK(snake.error().issues.front().reason ==
        R"(contains unknown member "next_cursor")");

  const Document document{.doc_title = "Notes", .audit_trail = {.created_at = "today"}};
  CHECK(encoded(document) == R"({"created_at":"today","docTitle":"Notes"})");
  CHECK(scrydantic::decode<Document>(encoded(document)).value() == document);
}

TEST_CASE("a variant alternative's rule leaves its tag as written") {
  const Block text{TextBlock{.body_text = "hi"}};
  CHECK(encoded(text) == R"({"bodyText":"hi","type":"text_block"})");
  CHECK(scrydantic::decode<Block>(encoded(text)).value() == text);
  const Block image{ImageBlock{.image_url = "u"}};
  CHECK(encoded(image) == R"({"image_url":"u","type":"image_block"})");
  CHECK(scrydantic::decode<Block>(encoded(image)).value() == image);

  const auto renamed_tag =
      scrydantic::decode<Block>(R"({"bodyText":"hi","type":"textBlock"})");
  REQUIRE_FALSE(renamed_tag);
  CHECK(renamed_tag.error().issues.front().kind == IssueKind::unknown_tag);
}

} // namespace
