// A variant's tag under a key of the alternatives' choosing (scrydantic::tag_key),
// and the alternative that an object without the key is (scrydantic::default_tag):
// their schemas, what encoding writes, how decoding selects and what it reports,
// where such a variant can stand in a model and a tool, the variants on "type"
// that neither annotation changes, and every use the compiler rejects, with its
// diagnostic.

#include <catch2/catch_test_macros.hpp>
#include <cstddef>
#include <cstdint>
#include <map>
#include <meta>
#include <optional>
#include <scrydantic/scrydantic.hpp>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

namespace {

using scrydantic::DecodeOptions;
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

// A request for input, told apart by "mode". The format had the form alone
// before it had the key, so an object without the key is a form. The guide's
// Variants paragraph shows these types, their schema, and what they encode and
// decode as, which the tests below hold it to.
struct[[
  = scrydantic::tag{"form"}, = scrydantic::tag_key{"mode"}, = scrydantic::default_tag
]] FormRequest {
  std::string message;

  bool operator==(const FormRequest&) const = default;
};

struct[[ = scrydantic::tag{"url"}, = scrydantic::tag_key{"mode"} ]] UrlRequest {
  std::string message;
  std::string url;

  bool operator==(const UrlRequest&) const = default;
};

using InputRequest = std::variant<FormRequest, UrlRequest>;

// Resources told apart by "kind", every one of which must say which it is. The
// key sorts before every member of the one and after every member of the other,
// and "type" is a member like any other.
struct[[ = scrydantic::tag{"Pod"}, = scrydantic::tag_key{"kind"} ]] Pod {
  std::string name;
  std::string type;

  bool operator==(const Pod&) const = default;
};

struct[[ = scrydantic::tag{"Job"}, = scrydantic::tag_key{"kind"} ]] Job {
  std::int32_t attempts{};

  bool operator==(const Job&) const = default;
};

using Resource = std::variant<Pod, Job>;

// Alternatives on "type", one of which an object without it is.
struct[[ = scrydantic::tag{"text"}, = scrydantic::default_tag ]] Text {
  std::string text;

  bool operator==(const Text&) const = default;
};

struct[[= scrydantic::tag{"image"}]] Image {
  std::string source;

  bool operator==(const Image&) const = default;
};

using Content = std::variant<Text, Image>;

// Alternatives on "type" that carry neither annotation.
struct[[= scrydantic::tag{"circle"}]] Circle {
  double radius{};

  bool operator==(const Circle&) const = default;
};

struct[[= scrydantic::tag{"square"}]] Square {
  double side{};

  bool operator==(const Square&) const = default;
};

using Shape = std::variant<Circle, Square>;

// The tagged aggregates beside alternatives of other kinds.
using Prompt = std::variant<std::monostate, std::string, FormRequest, UrlRequest>;
// A variant's only aggregate, which carries a tag all the same.
using Reply = std::variant<std::int32_t, FormRequest>;

// Where such a variant stands in a model.
struct Session {
  std::vector<InputRequest> requests;
  std::optional<Resource> owner;
  std::map<std::string, Content> notes;

  bool operator==(const Session&) const = default;
};

// A member that keeps the alternative its initializer holds.
struct Defaults {
  InputRequest request{FormRequest{.message = "Name?"}};
  Resource resource{Job{.attempts = 3}};
};

// An alternative that reads members it does not declare.
struct[[
  = scrydantic::tag{"open"}, = scrydantic::tag_key{"op"}, = scrydantic::ignore_unknown
]] Open {
  std::string path;
};

struct[[ = scrydantic::tag{"close"}, = scrydantic::tag_key{"op"} ]] Close {
  bool force{};
};

using Operation = std::variant<Open, Close>;

// A member inside an alternative that a constraint holds.
struct[[
  = scrydantic::tag{"retry"}, = scrydantic::tag_key{"action"}, = scrydantic::default_tag
]] Retry {
  [[= scrydantic::range{.min = 1, .max = 5}]] std::int32_t times{1};
};

struct[[ = scrydantic::tag{"abort"}, = scrydantic::tag_key{"action"} ]] Abort {
  std::string reason;
};

struct Policy {
  std::variant<Retry, Abort> on_failure;
};

// A rename_all spells the members' keys, and leaves the tag's key as written.
struct[[
  = scrydantic::tag{"done"}, = scrydantic::tag_key{"result_type"},
  = scrydantic::rename_all{scrydantic::camel_case}
]] Done {
  std::string final_text;
};

// A tag key that is not UTF-8.
struct[[ = scrydantic::tag{"plain"}, = scrydantic::tag_key{"k\xff"} ]] ByteKeyed {
  std::int32_t value{};
};

} // namespace

// A model that contains itself through an alternative, so its tagged form is a
// definition. In a named namespace, since a definition is named after its scope.
namespace tag_keys {

struct Group;
struct Leaf;
using Node = std::variant<Leaf, Group>;

struct[[
  = scrydantic::tag{"leaf"}, = scrydantic::tag_key{"node"}, = scrydantic::default_tag
]] Leaf {
  std::int32_t value{};

  bool operator==(const Leaf&) const = default;
};

struct[[ = scrydantic::tag{"group"}, = scrydantic::tag_key{"node"} ]] Group {
  std::vector<Node> children;

  bool operator==(const Group&) const = default;
};

} // namespace tag_keys

namespace tag_key_tools {

[[= scrydantic::tool{"Ask the user for input"}]] inline std::string
    ask(InputRequest request) {
  if (const auto* const url = std::get_if<UrlRequest>(&request)) {
    return "open " + url->url;
  }
  return "ask " + std::get<FormRequest>(request).message;
}

[[= scrydantic::tool{"The resource that replaces this one"}]] inline Resource
    replace(Resource resource) {
  if (const auto* const job = std::get_if<Job>(&resource)) {
    return Job{.attempts = job->attempts + 1};
  }
  return resource;
}

} // namespace tag_key_tools

namespace {

// ---- Families and metadata ---------------------------------------------------

static_assert(scrydantic::SupportedValue<InputRequest>);
static_assert(scrydantic::Encodable<InputRequest>);
static_assert(scrydantic::Decodable<InputRequest>);
static_assert(scrydantic::SupportedValue<Resource>);
static_assert(scrydantic::SupportedValue<Content>);
static_assert(scrydantic::SupportedValue<Prompt>);
static_assert(scrydantic::SupportedValue<Reply>);
static_assert(scrydantic::SupportedValue<Session>);
static_assert(scrydantic::SupportedValue<tag_keys::Node>);
// Outside a variant neither annotation has an effect, as the tag has none.
static_assert(scrydantic::SupportedValue<FormRequest>);
static_assert(
    scrydantic::schema_v<FormRequest> ==
    R"({"additionalProperties":false,"properties":{"message":{"type":"string"}},"required":["message"],"type":"object"})");

// tags_v lists the tags, whatever key they are written under and whichever
// alternative an object without the key is.
static_assert(scrydantic::tags_v<InputRequest>.size() == 2);
static_assert(scrydantic::tags_v<InputRequest>[0] == "form");
static_assert(scrydantic::tags_v<InputRequest>[1] == "url");
static_assert(scrydantic::tags_v<Resource>[0] == "Pod");
static_assert(scrydantic::tags_v<Content>[1] == "image");
// The tag's key is no member, so keys_v does not list it.
static_assert(scrydantic::keys_v<UrlRequest>.size() == 2);

// ---- Rejected at compile time ------------------------------------------------

struct[[ = scrydantic::tag{"a"}, = scrydantic::tag_key{"kind"} ]] KindA {
  bool flag{};
};

struct[[ = scrydantic::tag{"b"}, = scrydantic::tag_key{"mode"} ]] ModeB {
  bool flag{};
};

struct[[= scrydantic::tag{"c"}]] TypeC {
  bool flag{};
};

// The tagged alternatives of one variant name one key.
static_assert(mentions(
    scrydantic::supported_value_diagnostic<std::variant<KindA, ModeB>>(),
    "KindA and {anonymous}::ModeB that carry their "
    "tags under the keys \"kind\" and \"mode\"; the tagged alternatives of one "
    "variant name one scrydantic::tag_key"));
// One that names no key names "type".
static_assert(
    mentions(scrydantic::encodable_diagnostic<std::variant<TypeC, KindA>>(),
             "TypeC and {anonymous}::KindA that carry their tags under the keys "
             "\"type\" and \"kind\""));
static_assert(mentions(
    scrydantic::decodable_diagnostic<std::variant<std::string, KindA, TypeC>>(),
    "KindA and {anonymous}::TypeC that carry their tags under the keys "
    "\"kind\" and \"type\""));

struct[[ = scrydantic::tag{"first"}, = scrydantic::default_tag ]] FirstDefault {
  bool flag{};
};

struct[[ = scrydantic::tag{"second"}, = scrydantic::default_tag ]] SecondDefault {
  bool flag{};
};

// An object without the tag is one alternative.
static_assert(mentions(
    scrydantic::supported_value_diagnostic<std::variant<FirstDefault, SecondDefault>>(),
    "FirstDefault and {anonymous}::SecondDefault that both carry "
    "scrydantic::default_tag; an object without the tag is one alternative"));
static_assert(mentions(scrydantic::supported_value_diagnostic<
                           std::variant<FirstDefault, TypeC, SecondDefault>>(),
                       "FirstDefault and {anonymous}::SecondDefault that both "
                       "carry scrydantic::default_tag"));

// Each annotation says something of the class's tag.
struct[[= scrydantic::tag_key{"kind"}]] Keyed {
  bool flag{};
};

struct[[= scrydantic::default_tag]] Defaulted {
  bool flag{};
};

struct[[
  = scrydantic::tag{"twice"}, = scrydantic::tag_key{"kind"},
  = scrydantic::tag_key{"mode"}
]] TwiceKeyed {
  bool flag{};
};

static_assert(mentions(scrydantic::supported_value_diagnostic<Keyed>(),
                       "Keyed carries scrydantic::tag_key without a scrydantic::tag, "
                       "whose key it names"));
static_assert(
    mentions(scrydantic::supported_value_diagnostic<std::variant<std::string, Keyed>>(),
             "Keyed carries scrydantic::tag_key without a scrydantic::tag"));
static_assert(mentions(scrydantic::supported_value_diagnostic<Defaulted>(),
                       "Defaulted carries scrydantic::default_tag without a "
                       "scrydantic::tag, which it makes the tag of an object that "
                       "has none"));
static_assert(mentions(scrydantic::supported_value_diagnostic<TwiceKeyed>(),
                       "TwiceKeyed has more than one scrydantic::tag_key annotation"));

// Both apply to the class, not to a member of it.
struct KeyedMember {
  [[= scrydantic::tag_key{"kind"}]] Shape shape;
};

struct DefaultedMember {
  [[= scrydantic::default_tag]] Shape shape;
};

static_assert(mentions(scrydantic::supported_value_diagnostic<KeyedMember>(),
                       "KeyedMember::shape: carries scrydantic::tag, tag_key, "
                       "default_tag, or ignore_unknown, which apply to class types"));
static_assert(mentions(scrydantic::supported_value_diagnostic<DefaultedMember>(),
                       "DefaultedMember::shape: carries scrydantic::tag, tag_key, "
                       "default_tag, or ignore_unknown, which apply to class types"));

// No member of an alternative has the key that carries its tag: one it
// declares, one a name gives it, one a rename_all derives, or one that a
// flattened member brings.
struct[[ = scrydantic::tag{"direct"}, = scrydantic::tag_key{"mode"} ]] Direct {
  std::string mode;
};

struct[[ = scrydantic::tag{"named"}, = scrydantic::tag_key{"mode"} ]] Named {
  [[= scrydantic::name{"mode"}]] std::string how;
};

struct[[
  = scrydantic::tag{"renamed"}, = scrydantic::tag_key{"resultType"},
  = scrydantic::rename_all{scrydantic::camel_case}
]] Renamed {
  std::string result_type;
};

struct Mode {
  std::string mode;
};

struct[[ = scrydantic::tag{"flat"}, = scrydantic::tag_key{"mode"} ]] Flat {
  [[= scrydantic::flatten]] Mode inner;
};

static_assert(mentions(scrydantic::supported_value_diagnostic<std::variant<Direct>>(),
                       "Direct::mode: has the JSON key \"mode\", which carries the "
                       "variant's tag"));
static_assert(mentions(scrydantic::supported_value_diagnostic<std::variant<Named>>(),
                       "Named::how: has the JSON key \"mode\", which carries the "
                       "variant's tag"));
static_assert(mentions(scrydantic::supported_value_diagnostic<std::variant<Renamed>>(),
                       "Renamed::result_type: has the JSON key \"resultType\", which "
                       "carries the variant's tag"));
static_assert(mentions(scrydantic::supported_value_diagnostic<std::variant<Flat>>(),
                       "Flat::inner.mode: has the JSON key \"mode\", which carries "
                       "the variant's tag"));
// Under another key, "type" is an alternative's own to use, as Pod does; and
// the alternative on "type" still may not.
struct[[= scrydantic::tag{"typed"}]] Typed {
  std::string type;
};
static_assert(mentions(scrydantic::supported_value_diagnostic<std::variant<Typed>>(),
                       "Typed::type: has the JSON key \"type\", which carries the "
                       "variant's tag"));

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

// The value that `text` decodes as, which must succeed.
template <typename Type> [[nodiscard]] Type decoded(const std::string_view text) {
  CAPTURE(text);
  auto value = scrydantic::decode<Type>(text);
  REQUIRE(value);
  return std::move(*value);
}

template <typename Type> [[nodiscard]] std::string encoded(const Type& value) {
  const auto json = scrydantic::encode(value);
  REQUIRE(json);
  return json->text;
}

template <typename... Schemas>
[[nodiscard]] std::string any_of(const Schemas&... schemas) {
  std::string text{R"({"anyOf":[)"};
  bool first = true;
  for (const std::string_view schema : {std::string_view{schemas}...}) {
    if (!first) {
      text += ',';
    }
    text += schema;
    first = false;
  }
  return text + "]}";
}

// The tag property stands under the key, in the form it has under "type", and
// the alternative that an object without it is does not require it.
constexpr std::string_view form_schema =
    R"({"additionalProperties":false,"properties":{"message":{"type":"string"},"mode":{"enum":["form"],"type":"string"}},"required":["message"],"type":"object"})";
constexpr std::string_view url_schema =
    R"({"additionalProperties":false,"properties":{"message":{"type":"string"},"mode":{"enum":["url"],"type":"string"},"url":{"type":"string"}},"required":["message","mode","url"],"type":"object"})";
constexpr std::string_view pod_schema =
    R"({"additionalProperties":false,"properties":{"kind":{"enum":["Pod"],"type":"string"},"name":{"type":"string"},"type":{"type":"string"}},"required":["kind","name","type"],"type":"object"})";
constexpr std::string_view job_schema =
    R"({"additionalProperties":false,"properties":{"attempts":{"default":0,"maximum":2147483647,"minimum":-2147483648,"type":"integer"},"kind":{"enum":["Job"],"type":"string"}},"required":["kind"],"type":"object"})";

// ---- The key -----------------------------------------------------------------

TEST_CASE("a tagged variant carries its tag under the key its alternatives name") {
  CHECK(scrydantic::schema_v<InputRequest> == any_of(form_schema, url_schema));
  CHECK(scrydantic::schema_v<Resource> == any_of(pod_schema, job_schema));

  // The tag is written at its key's place among the members' keys.
  CHECK(encoded(InputRequest{FormRequest{.message = "Name?"}}) ==
        R"({"message":"Name?","mode":"form"})");
  CHECK(encoded(InputRequest{UrlRequest{.message = "Sign in", .url = "https://e"}}) ==
        R"({"message":"Sign in","mode":"url","url":"https://e"})");
  CHECK(encoded(Resource{Pod{.name = "web", .type = "small"}}) ==
        R"({"kind":"Pod","name":"web","type":"small"})");
  CHECK(encoded(Resource{Job{.attempts = 2}}) == R"({"attempts":2,"kind":"Job"})");

  for (const std::string_view text :
       {R"({"message":"Name?","mode":"form"})",
        R"({"message":"Sign in","mode":"url","url":"https://e"})"}) {
    CHECK(encoded(decoded<InputRequest>(text)) == text);
  }
  CHECK(decoded<InputRequest>(R"({"mode":"url","url":"u","message":"m"})") ==
        InputRequest{UrlRequest{.message = "m", .url = "u"}});
  CHECK(decoded<Resource>(R"({"type":"small","name":"web","kind":"Pod"})") ==
        Resource{Pod{.name = "web", .type = "small"}});
  CHECK(decoded<Resource>(R"({"kind":"Job"})") == Resource{Job{}});

  CHECK(encoded(std::variant<Done>{Done{.final_text = "ok"}}) ==
        R"({"finalText":"ok","result_type":"done"})");
}

TEST_CASE("a missing or unknown tag is reported at the key") {
  CHECK(rejection<Resource>(R"({"name":"web","type":"small"})") ==
        Lines{"missing_tag $.kind is a required member; must be one of: Pod, Job"});
  CHECK(rejection<Resource>(R"({"kind":"Cron","attempts":1})") ==
        Lines{"unknown_tag $.kind must be one of: Pod, Job"});
  CHECK(rejection<Resource>(R"({"kind":7})") ==
        Lines{"unknown_tag $.kind must be one of: Pod, Job"});
  CHECK(rejection<Resource>("[]") == Lines{"wrong_type $ must be an object"});

  // The selected alternative reports its own issues, and the key is not one of
  // the members it does not know. "type" is, where it is not the key.
  CHECK(rejection<Resource>(R"({"kind":"Job","attempts":"2","type":"Job"})") ==
        Lines{"unknown_member $ contains unknown member \"type\"",
              "wrong_type $.attempts must be an integer"});
  CHECK(rejection<Resource>(R"({"kind":"Pod","name":"web"})") ==
        Lines{"missing_member $.type is a required member"});
  CHECK(rejection<InputRequest>(R"({"type":"form","message":"m","mode":"form"})") ==
        Lines{"unknown_member $ contains unknown member \"type\""});
}

TEST_CASE("an alternative that ignores unknown members still reads its tag's key") {
  const auto open = decoded<Operation>(R"({"op":"open","path":"/tmp","mode":"r"})");
  CHECK(std::get<Open>(open).path == "/tmp");
  CHECK(encoded(open) == R"({"op":"open","path":"/tmp"})");
  CHECK(rejection<Operation>(R"({"op":"close","mode":"r"})") ==
        Lines{"unknown_member $ contains unknown member \"mode\""});
  CHECK(rejection<Operation>(R"({"path":"/tmp"})") ==
        Lines{"missing_tag $.op is a required member; must be one of: open, close"});
}

// ---- The alternative for a missing tag ---------------------------------------

TEST_CASE("an object without the key is the alternative that carries default_tag") {
  CHECK(decoded<InputRequest>(R"({"message":"hi"})") ==
        InputRequest{FormRequest{.message = "hi"}});
  // Encoding writes the tag all the same.
  CHECK(encoded(decoded<InputRequest>(R"({"message":"hi"})")) ==
        R"({"message":"hi","mode":"form"})");
  // With the key the object is the alternative its tag names, the default too.
  CHECK(decoded<InputRequest>(R"({"message":"hi","mode":"form"})").index() == 0);
  CHECK(decoded<InputRequest>(R"({"message":"hi","mode":"url","url":"u"})").index() ==
        1);

  // A key with a value that is no alternative's tag is still unknown.
  CHECK(rejection<InputRequest>(R"({"message":"hi","mode":"fax"})") ==
        Lines{"unknown_tag $.mode must be one of: form, url"});
  CHECK(rejection<InputRequest>(R"({"message":"hi","mode":null})") ==
        Lines{"unknown_tag $.mode must be one of: form, url"});
  const auto fax = scrydantic::decode<InputRequest>(R"({"message":"hi","mode":"fax"})");
  REQUIRE_FALSE(fax);
  CHECK(fax.error().model_message == "$.mode must be one of: form, url");

  // Without the key the object is held to the default alternative, whose issues
  // are the only ones.
  CHECK(rejection<InputRequest>("{}") ==
        Lines{"missing_member $.message is a required member"});
  CHECK(rejection<InputRequest>(R"({"message":"hi","url":"u"})") ==
        Lines{"unknown_member $ contains unknown member \"url\""});
  CHECK(rejection<InputRequest>(R"({"message":7})") ==
        Lines{"wrong_type $.message must be a string"});
}

TEST_CASE("default_tag selects an alternative on \"type\" as on any key") {
  CHECK(
      scrydantic::schema_v<Content> ==
      any_of(
          R"({"additionalProperties":false,"properties":{"text":{"type":"string"},"type":{"enum":["text"],"type":"string"}},"required":["text"],"type":"object"})",
          R"({"additionalProperties":false,"properties":{"source":{"type":"string"},"type":{"enum":["image"],"type":"string"}},"required":["source","type"],"type":"object"})"));
  CHECK(decoded<Content>(R"({"text":"hi"})") == Content{Text{.text = "hi"}});
  CHECK(encoded(Content{Text{.text = "hi"}}) == R"({"text":"hi","type":"text"})");
  CHECK(decoded<Content>(R"({"source":"a.png","type":"image"})").index() == 1);
  CHECK(rejection<Content>(R"({"source":"a.png"})") ==
        Lines{"unknown_member $ contains unknown member \"source\"",
              "missing_member $.text is a required member"});
  CHECK(rejection<Content>(R"({"text":"hi","type":"video"})") ==
        Lines{"unknown_tag $.type must be one of: text, image"});
}

TEST_CASE("a variant on \"type\" without either annotation is unchanged") {
  CHECK(
      scrydantic::schema_v<Shape> ==
      any_of(
          R"({"additionalProperties":false,"properties":{"radius":{"default":0,"type":"number"},"type":{"enum":["circle"],"type":"string"}},"required":["type"],"type":"object"})",
          R"({"additionalProperties":false,"properties":{"side":{"default":0,"type":"number"},"type":{"enum":["square"],"type":"string"}},"required":["type"],"type":"object"})"));
  CHECK(encoded(Shape{Circle{.radius = 2}}) == R"({"radius":2,"type":"circle"})");
  CHECK(decoded<Shape>(R"({"side":1.5,"type":"square"})") ==
        Shape{Square{.side = 1.5}});
  CHECK(rejection<Shape>(R"({"radius":2})") ==
        Lines{"missing_tag $.type is a required member; must be one of: circle, "
              "square"});
  CHECK(rejection<Shape>(R"({"type":"oval"})") ==
        Lines{"unknown_tag $.type must be one of: circle, square"});
  CHECK(rejection<Shape>(R"({"type":"circle","mode":"circle"})") ==
        Lines{"unknown_member $ contains unknown member \"mode\""});
}

// ---- Beside other kinds ------------------------------------------------------

TEST_CASE("the tagged group of a variant told apart by kind follows the same rules") {
  CHECK(scrydantic::schema_v<Prompt> ==
        any_of(R"({"type":"null"})", R"({"type":"string"})", form_schema, url_schema));
  CHECK(decoded<Prompt>("null").index() == 0);
  CHECK(decoded<Prompt>(R"("mode")").index() == 1);
  CHECK(decoded<Prompt>(R"({"message":"hi"})") == Prompt{FormRequest{.message = "hi"}});
  CHECK(decoded<Prompt>(R"({"message":"hi","mode":"url","url":"u"})").index() == 3);
  CHECK(encoded(Prompt{FormRequest{.message = "hi"}}) ==
        R"({"message":"hi","mode":"form"})");
  CHECK(rejection<Prompt>(R"({"message":"hi","mode":"fax"})") ==
        Lines{"unknown_tag $.mode must be one of: form, url"});
  CHECK(rejection<Prompt>("7") ==
        Lines{"wrong_type $ must be null, a string, or an object"});

  // A variant's only aggregate that carries a tag is read by it too.
  CHECK(scrydantic::schema_v<Reply> ==
        any_of(R"({"maximum":2147483647,"minimum":-2147483648,"type":"integer"})",
               form_schema));
  CHECK(decoded<Reply>(R"({"message":"hi"})").index() == 1);
  CHECK(encoded(Reply{FormRequest{.message = "hi"}}) ==
        R"({"message":"hi","mode":"form"})");
  CHECK(rejection<Reply>(R"({"message":"hi","mode":"url"})") ==
        Lines{"unknown_tag $.mode must be one of: form"});
}

// ---- Nesting -----------------------------------------------------------------

TEST_CASE("a variant on another key nests in a vector, an optional, and a map") {
  const Session session{
      .requests = {FormRequest{.message = "a"}, UrlRequest{.message = "b", .url = "u"}},
      .owner = Job{.attempts = 1},
      .notes = {{"first", Text{.text = "t"}}, {"second", Image{.source = "s"}}},
  };
  const std::string_view text =
      R"({"notes":{"first":{"text":"t","type":"text"},"second":{"source":"s","type":"image"}},)"
      R"("owner":{"attempts":1,"kind":"Job"},)"
      R"("requests":[{"message":"a","mode":"form"},{"message":"b","mode":"url","url":"u"}]})";
  CHECK(encoded(session) == text);
  CHECK(decoded<Session>(text) == session);
  // Each object without its key is its variant's default alternative.
  CHECK(
      decoded<Session>(
          R"({"notes":{"first":{"text":"t"}},"owner":null,"requests":[{"message":"a"}]})") ==
      Session{.requests = {FormRequest{.message = "a"}},
              .owner = std::nullopt,
              .notes = {{"first", Text{.text = "t"}}}});

  CHECK(rejection<Session>(
            R"({"notes":{"n":{"type":"audio"}},"owner":{"attempts":1},)"
            R"("requests":[{"message":"a"},{"message":"b","mode":"fax"}]})") ==
        Lines{"unknown_tag $.requests[1].mode must be one of: form, url",
              "missing_tag $.owner.kind is a required member; must be one of: Pod, "
              "Job",
              "unknown_tag $.notes.n.type must be one of: text, image"});

  CHECK(mentions(scrydantic::schema_v<Session>,
                 R"("owner":{"anyOf":[)" + std::string{pod_schema} + "," +
                     std::string{job_schema} + R"(,{"type":"null"}]})"));
}

TEST_CASE("a schema states a member's default with the tag under its key") {
  CHECK(mentions(scrydantic::schema_v<Defaults>,
                 R"(],"default":{"message":"Name?","mode":"form"}})"));
  CHECK(mentions(scrydantic::schema_v<Defaults>,
                 R"(],"default":{"attempts":3,"kind":"Job"}})"));
  CHECK(
      encoded(decoded<Defaults>("{}")) ==
      R"({"request":{"message":"Name?","mode":"form"},"resource":{"attempts":3,"kind":"Job"}})");
}

TEST_CASE(
    "a model that contains itself names its tagged form's key in the definition") {
  using tag_keys::Group;
  using tag_keys::Leaf;
  using tag_keys::Node;
  CHECK(
      scrydantic::schema_v<Node> ==
      R"({"$defs":{"tag_keys.Group":{"additionalProperties":false,"properties":{"children":{"items":{"anyOf":[{"additionalProperties":false,"properties":{"node":{"enum":["leaf"],"type":"string"},"value":{"default":0,"maximum":2147483647,"minimum":-2147483648,"type":"integer"}},"required":[],"type":"object"},{"$ref":"#/$defs/tag_keys.Group"}]},"type":"array"},"node":{"enum":["group"],"type":"string"}},"required":["children","node"],"type":"object"}},"anyOf":[{"additionalProperties":false,"properties":{"node":{"enum":["leaf"],"type":"string"},"value":{"default":0,"maximum":2147483647,"minimum":-2147483648,"type":"integer"}},"required":[],"type":"object"},{"$ref":"#/$defs/tag_keys.Group"}]})");
  const Node tree{Group{.children = {Leaf{.value = 1}, Group{.children = {Leaf{}}}}}};
  const std::string_view text =
      R"({"children":[{"node":"leaf","value":1},{"children":[{"node":"leaf","value":0}],"node":"group"}],"node":"group"})";
  CHECK(encoded(tree) == text);
  CHECK(decoded<Node>(text) == tree);
  CHECK(
      decoded<Node>(
          R"({"children":[{"value":1},{"children":[{}],"node":"group"}],"node":"group"})") ==
      tree);
  CHECK(
      rejection<Node>(
          R"({"children":[{"children":[{"node":"twig"}],"node":"group"}],"node":"group"})") ==
      Lines{"unknown_tag $.children[0].children[0].node must be one of: leaf, "
            "group"});
}

// ---- With the rest of the codec ----------------------------------------------

TEST_CASE("a string that spells an object is read by the key as the object is") {
  const DecodeOptions spelled{.coerce_structures = true};
  const auto form =
      scrydantic::decode<InputRequest>(R"("{\"message\":\"hi\"}")", spelled);
  REQUIRE(form);
  CHECK(*form == InputRequest{FormRequest{.message = "hi"}});
  CHECK(
      rejection<InputRequest>(R"("{\"message\":\"hi\",\"mode\":\"fax\"}")", spelled) ==
      Lines{"unknown_tag $.mode must be one of: form, url"});
}

TEST_CASE("validate() checks the alternative held, whatever its tag's key") {
  CHECK(scrydantic::validate(Policy{.on_failure = Retry{.times = 3}}));
  const auto status = scrydantic::validate(Policy{.on_failure = Retry{.times = 9}});
  REQUIRE_FALSE(status);
  CHECK(status.error().model_message == "$.on_failure.times must be at most 5");
  // The value an object without the key decodes to is checked like any other.
  CHECK(rejection<Policy>(R"({"on_failure":{"times":0}})") ==
        Lines{"violated_constraint $.on_failure.times must be at least 1"});
  CHECK(encoded(decoded<Policy>(R"({"on_failure":{}})")) ==
        R"({"on_failure":{"action":"retry","times":1}})");
}

TEST_CASE("a tag key that is not UTF-8 is refused when it would be written") {
  const auto refused = scrydantic::encode(std::variant<ByteKeyed>{ByteKeyed{}});
  REQUIRE_FALSE(refused);
  CHECK(refused.error().message ==
        "the value holds a string that is not valid UTF-8, so it cannot be JSON");
  // Outside a variant the key is not written.
  CHECK(encoded(ByteKeyed{.value = 1}) == R"({"value":1})");
}

// ---- Tools -------------------------------------------------------------------

TEST_CASE("a tool takes a variant on another key as a parameter and returns one") {
  const auto tools = scrydantic::bind<^^tag_key_tools>();
  REQUIRE(tools.size() == 2);

  CHECK(tools[0].name == "ask");
  CHECK(tools[0].input_schema.text ==
        R"({"additionalProperties":false,"properties":{"request":)" +
            std::string{scrydantic::schema_v<InputRequest>} +
            R"(},"required":["request"],"type":"object"})");
  const auto url = tools[0].invoke(
      Json{.text = R"({"request":{"message":"m","mode":"url","url":"https://e"}})"});
  REQUIRE(url);
  CHECK(url->text == R"("open https://e")");
  const auto form = tools[0].invoke(Json{.text = R"({"request":{"message":"Name?"}})"});
  REQUIRE(form);
  CHECK(form->text == R"("ask Name?")");
  const auto fax =
      tools[0].invoke(Json{.text = R"({"request":{"message":"m","mode":"fax"}})"});
  REQUIRE_FALSE(fax);
  CHECK(fax.error().model_message == "$.request.mode must be one of: form, url");

  CHECK(tools[1].name == "replace");
  REQUIRE(tools[1].output_schema.has_value());
  CHECK(tools[1].output_schema->text == scrydantic::schema_v<Resource>);
  const auto job =
      tools[1].invoke(Json{.text = R"({"resource":{"attempts":1,"kind":"Job"}})"});
  REQUIRE(job);
  CHECK(job->text == R"({"attempts":2,"kind":"Job"})");
  const auto untagged = tools[1].invoke(Json{.text = R"({"resource":{"attempts":1}})"});
  REQUIRE_FALSE(untagged);
  CHECK(untagged.error().model_message ==
        "$.resource.kind is a required member; must be one of: Pod, Job");
}

TEST_CASE("a typed handler takes an aggregate that holds such a variant") {
  const auto count = scrydantic::bind_handler<Session>(
      [](const Session& session) { return session.requests.size(); });
  const auto result = count(Json{
      .text =
          R"({"notes":{},"owner":null,"requests":[{"message":"a"},{"message":"b","mode":"form"}]})"});
  REQUIRE(result);
  CHECK(result->text == "2");
}

} // namespace
