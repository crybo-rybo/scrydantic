#include <algorithm>
#include <array>
#include <bit>
#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_tostring.hpp>
#include <charconv>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <memory>
#include <memory_resource>
#include <meta>
#include <optional>
#include <random>
#include <scrydantic/annotations.hpp>
#include <scrydantic/codec.hpp>
#include <scrydantic/detail/encode.hpp>
#include <scrydantic/detail/json_text.hpp>
#include <scrydantic/error.hpp>
#include <scrydantic/json.hpp>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <variant>
#include <vector>

namespace {

// ---- Tagged variants ---------------------------------------------------------

struct[[= scrydantic::tag{"circle"}]] Circle {
  double radius{};
};

// "height" sorts before the "type" tag key and "width" after it, so the tag lands
// between them.
struct[[= scrydantic::tag{"rectangle"}]] Rectangle {
  double height{};
  double width{};
};

using Shape = std::variant<Circle, Rectangle>;

struct Drawing {
  [[= scrydantic::description{"Shapes to draw"}]] std::vector<Shape> shapes;
  std::optional<Shape> highlight = std::nullopt;
};

// Alternatives with no members, and one whose only key sorts after the tag.
struct[[= scrydantic::tag{"empty"}]] EmptyAlternative {};

struct[[= scrydantic::tag{"late"}]] LateAlternative {
  int zulu{};
};

using Edge = std::variant<EmptyAlternative, LateAlternative>;

// ---- Keys and null policy ----------------------------------------------------

struct Renamed {
  [[= scrydantic::name{"max_tokens"}]] std::uint32_t limit{};
  // One bracket may hold several annotations, each introduced by its own `=`.
  [[
    = scrydantic::name{"a_first"},
    = scrydantic::description{"Sorted by its key"}
  ]] std::string zed;
};

struct[[= scrydantic::skip_null]] Sparse {
  std::optional<int> absent;
  [[= scrydantic::emit_null]] std::optional<int> explicit_null;
  std::optional<int> initialized = 3;
  int plain{};
};

struct MemberSkip {
  [[= scrydantic::skip_null]] std::optional<std::string> note;
  std::optional<std::string> other;
};

// ---- Lenient decoding --------------------------------------------------------

struct[[= scrydantic::ignore_unknown]] LenientInner {
  int kept{};
};

struct[[= scrydantic::ignore_unknown]] LenientEvent {
  LenientInner inner;
  std::string kind;
};

struct StrictHolder {
  LenientInner inner;
};

struct[[= scrydantic::tag{"delta"}]][[= scrydantic::ignore_unknown]] LenientDelta {
  std::string text;
};

struct[[= scrydantic::tag{"stop"}]] StrictStop {};

using StreamEvent = std::variant<LenientDelta, StrictStop>;

// ---- Encode-only and document-like shapes ------------------------------------

struct BorrowedTool {
  std::string_view description{};
  scrydantic::Json input_schema{};
  std::string_view name{};
};

struct[[= scrydantic::skip_null]] BorrowedRequest {
  std::optional<std::uint32_t> max_tokens{};
  std::string_view model{};
  bool stream{true};
  std::optional<std::string_view> system{};
  double temperature{};
  std::optional<std::vector<BorrowedTool>> tools{};
  std::optional<double> top_p{};
};

enum class Role {
  user,
  assistant,
};

struct[[= scrydantic::tag{"text"}]] DocText {
  std::string text;
};

struct[[= scrydantic::tag{"tool_call"}]] DocToolCall {
  scrydantic::Json arguments;
  std::string id;
  std::string name;
};

struct[[= scrydantic::tag{"tool_result"}]] DocToolResult {
  bool is_error{};
  scrydantic::Json result;
  std::string tool_call_id;
};

using DocBlock = std::variant<DocText, DocToolCall, DocToolResult>;

struct DocMessage {
  std::vector<DocBlock> content;
  Role role{};
};

struct Document {
  std::vector<DocMessage> messages;
  std::string system_prompt;
  std::uint64_t version{};
};

// ---- Families ----------------------------------------------------------------

struct ConstView {
  const int value;
};

struct Payload {
  scrydantic::Json body;
};

// Rejected shapes, checked through the diagnostics the entry points print.
struct Unannotated {
  int value{};
};

struct[[= scrydantic::tag{"left"}]] Left {
  int value{};
};

struct[[= scrydantic::tag{"left"}]] AlsoLeft {
  int other{};
};

struct[[= scrydantic::tag{"typed"}]] TypedAlternative {
  [[= scrydantic::name{"type"}]] std::string kind;
};

struct Colliding {
  [[= scrydantic::name{"key"}]] int first{};
  [[= scrydantic::name{"key"}]] int second{};
};

struct MisplacedSkip {
  [[= scrydantic::skip_null]] int value{};
};

struct HoldsView {
  std::string_view text;
};

struct Nested {
  std::vector<std::optional<wchar_t>> letters;
};

struct Outer {
  Nested nested;
};

// std::uint8_t is unsigned char, a character type, so the diagnostics that
// suggest a replacement must not suggest it.
struct Bytes {
  std::vector<std::uint8_t> data;
};

struct Flags {
  std::vector<bool> flags;
};

struct FlagsFixed {
  std::array<bool, 4> flags{};
};

struct Words {
  std::vector<std::uint16_t> words;
};

struct[[= scrydantic::tag{"bad"}]] BadAlternative {
  char letter{};
};

struct HoldsBad {
  std::vector<std::variant<BadAlternative>> items;
};

template <typename Type>
[[nodiscard]] scrydantic::Result<Type> decode_text(const std::string_view text) {
  return scrydantic::decode<Type>(scrydantic::Json{.text = std::string{text}});
}

} // namespace

// ---- Family membership -------------------------------------------------------

static_assert(scrydantic::SupportedValue<Shape>);
static_assert(scrydantic::SupportedValue<Drawing>);
static_assert(scrydantic::ToolArguments<Drawing>);
static_assert(scrydantic::SupportedValue<Edge>);
static_assert(scrydantic::SupportedValue<Renamed>);
static_assert(scrydantic::SupportedValue<Sparse>);
static_assert(scrydantic::SupportedValue<StreamEvent>);
static_assert(!scrydantic::ToolArguments<Shape>);

static_assert(scrydantic::Encodable<BorrowedRequest>);
static_assert(!scrydantic::Decodable<BorrowedRequest>);
static_assert(!scrydantic::SupportedValue<BorrowedRequest>);

static_assert(scrydantic::Encodable<Document>);
static_assert(scrydantic::Decodable<Document>);
static_assert(!scrydantic::SupportedValue<Document>);
static_assert(!scrydantic::ToolArguments<Document>);

static_assert(scrydantic::Encodable<ConstView>);
static_assert(!scrydantic::Decodable<ConstView>);
static_assert(scrydantic::Decodable<Payload>);
static_assert(scrydantic::Encodable<scrydantic::Json>);
static_assert(scrydantic::Decodable<std::optional<scrydantic::Json>>);
static_assert(!scrydantic::SupportedValue<scrydantic::Json>);
static_assert(scrydantic::Encodable<std::string_view>);
static_assert(!scrydantic::Decodable<std::string_view>);

// decode<T>() produces a value, so its target is a cv-unqualified object type, and
// the concept agrees with the entry point's check.
static_assert(!scrydantic::Decodable<const Payload>);
static_assert(!scrydantic::Decodable<volatile Payload>);
static_assert(!scrydantic::Decodable<Payload&>);
static_assert(!scrydantic::Decodable<const Payload&>);
static_assert(!scrydantic::Decodable<Payload&&>);

namespace constrained {

// A wrapper constrained on the concept, as the concepts' documentation suggests,
// never reaches decode<T>() with a target decode<T>() rejects.
template <typename Type>
  requires scrydantic::Decodable<Type>
scrydantic::Result<Type> decode_checked(const scrydantic::Json& json) {
  return scrydantic::decode<Type>(json);
}

template <typename Type>
concept checked_decodable =
    requires(const scrydantic::Json& json) { decode_checked<Type>(json); };

static_assert(checked_decodable<Payload>);
static_assert(!checked_decodable<const Payload>);
static_assert(!checked_decodable<Payload&>);

} // namespace constrained

static_assert(!scrydantic::SupportedValue<std::variant<Unannotated>>);
static_assert(!scrydantic::SupportedValue<std::variant<Left, int>>);
static_assert(!scrydantic::SupportedValue<std::variant<Left, AlsoLeft>>);
static_assert(!scrydantic::SupportedValue<std::variant<TypedAlternative>>);
static_assert(!scrydantic::SupportedValue<Colliding>);
static_assert(!scrydantic::SupportedValue<MisplacedSkip>);
// A tagged class outside a variant, and a type key outside one, are ordinary.
static_assert(scrydantic::SupportedValue<Left>);
static_assert(scrydantic::SupportedValue<TypedAlternative>);

// ---- Diagnostics -------------------------------------------------------------

namespace {

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

} // namespace

static_assert(mentions(scrydantic::detail::supported_diagnostic<Outer>(),
                       "does not satisfy scrydantic::SupportedValue: "));
static_assert(mentions(scrydantic::detail::supported_diagnostic<Outer>(),
                       "Outer::nested.letters[]: wchar_t is a character type"));
static_assert(mentions(scrydantic::detail::supported_diagnostic<HoldsBad>(),
                       "HoldsBad::items[]<"));
static_assert(mentions(scrydantic::detail::supported_diagnostic<HoldsBad>(),
                       "BadAlternative>.letter: char is a character type"));
static_assert(
    mentions(scrydantic::detail::supported_diagnostic<std::variant<Unannotated>>(),
             "without a scrydantic::tag annotation"));
static_assert(
    mentions(scrydantic::detail::supported_diagnostic<std::variant<Left, int>>(),
             "has the alternative int, which is not a plain aggregate"));
static_assert(
    mentions(scrydantic::detail::supported_diagnostic<std::variant<Left, AlsoLeft>>(),
             "that share the tag \"left\""));
static_assert(
    mentions(scrydantic::detail::supported_diagnostic<std::variant<TypedAlternative>>(),
             "TypedAlternative::kind: has the JSON key \"type\""));
static_assert(mentions(scrydantic::detail::supported_diagnostic<Colliding>(),
                       "has members first and second that both map to the JSON key "
                       "\"key\""));
static_assert(mentions(scrydantic::detail::supported_diagnostic<MisplacedSkip>(),
                       "MisplacedSkip::value: is not a std::optional"));
static_assert(mentions(scrydantic::detail::supported_diagnostic<HoldsView>(),
                       "HoldsView::text: "));
static_assert(mentions(scrydantic::detail::supported_diagnostic<HoldsView>(),
                       " is encode-only borrowed text; decoding and schemas take "
                       "std::string"));
static_assert(mentions(scrydantic::detail::decodable_diagnostic<ConstView>(),
                       "ConstView::value: is a const member"));
static_assert(mentions(scrydantic::detail::supported_diagnostic<Document>(),
                       "scrydantic::Json is raw JSON, which has no schema"));
static_assert(mentions(scrydantic::detail::tool_arguments_diagnostic<Shape>(),
                       "is not a plain aggregate; tool arguments decode from one "
                       "JSON object"));
static_assert(mentions(scrydantic::detail::supported_diagnostic<Bytes>(),
                       "Bytes::data[]: unsigned char is a character type, as "
                       "std::int8_t and std::uint8_t are; use std::int16_t or "
                       "std::uint16_t for a number"));
static_assert(mentions(scrydantic::detail::supported_diagnostic<Flags>(),
                       "Flags::flags: std::vector<bool> is std::vector<bool>, whose "
                       "elements are not addressable; use std::vector<std::uint16_t>, "
                       "or std::array<bool, N> for a fixed size"));
// Each replacement the diagnostics suggest is itself supported.
static_assert(scrydantic::SupportedValue<Words>);
static_assert(scrydantic::SupportedValue<FlagsFixed>);
static_assert(scrydantic::detail::supported_diagnostic<Drawing>().empty());

// ---- Schemas -----------------------------------------------------------------

static_assert(
    scrydantic::schema_v<Shape> ==
    R"({"anyOf":[{"additionalProperties":false,"properties":{"radius":{"type":"number"},"type":{"enum":["circle"],"type":"string"}},"required":["type"],"type":"object"},{"additionalProperties":false,"properties":{"height":{"type":"number"},"type":{"enum":["rectangle"],"type":"string"},"width":{"type":"number"}},"required":["type"],"type":"object"}]})");

// An optional variant joins null to the alternatives; a described variant keeps
// its description beside anyOf.
static_assert(
    scrydantic::input_schema_v<Drawing> ==
    R"({"additionalProperties":false,"properties":{"highlight":{"anyOf":[{"additionalProperties":false,"properties":{"radius":{"type":"number"},"type":{"enum":["circle"],"type":"string"}},"required":["type"],"type":"object"},{"additionalProperties":false,"properties":{"height":{"type":"number"},"type":{"enum":["rectangle"],"type":"string"},"width":{"type":"number"}},"required":["type"],"type":"object"},{"type":"null"}]},"shapes":{"description":"Shapes to draw","items":{"anyOf":[{"additionalProperties":false,"properties":{"radius":{"type":"number"},"type":{"enum":["circle"],"type":"string"}},"required":["type"],"type":"object"},{"additionalProperties":false,"properties":{"height":{"type":"number"},"type":{"enum":["rectangle"],"type":"string"},"width":{"type":"number"}},"required":["type"],"type":"object"}]},"type":"array"}},"required":["shapes"],"type":"object"})");

static_assert(
    scrydantic::schema_v<Edge> ==
    R"({"anyOf":[{"additionalProperties":false,"properties":{"type":{"enum":["empty"],"type":"string"}},"required":["type"],"type":"object"},{"additionalProperties":false,"properties":{"type":{"enum":["late"],"type":"string"},"zulu":{"maximum":2147483647,"minimum":-2147483648,"type":"integer"}},"required":["type"],"type":"object"}]})");

static_assert(
    scrydantic::schema_v<Renamed> ==
    R"({"additionalProperties":false,"properties":{"a_first":{"description":"Sorted by its key","type":"string"},"max_tokens":{"maximum":4294967295,"minimum":0,"type":"integer"}},"required":["a_first"],"type":"object"})");

// A skip_null member may be absent, so only the emit_null one stays required.
static_assert(
    scrydantic::schema_v<Sparse> ==
    R"({"additionalProperties":false,"properties":{"absent":{"anyOf":[{"maximum":2147483647,"minimum":-2147483648,"type":"integer"},{"type":"null"}]},"explicit_null":{"anyOf":[{"maximum":2147483647,"minimum":-2147483648,"type":"integer"},{"type":"null"}]},"initialized":{"anyOf":[{"maximum":2147483647,"minimum":-2147483648,"type":"integer"},{"type":"null"}]},"plain":{"maximum":2147483647,"minimum":-2147483648,"type":"integer"}},"required":["explicit_null"],"type":"object"})");

// Lenient decoding does not open the schema: it describes what a producer sends.
static_assert(
    scrydantic::schema_v<LenientInner> ==
    R"({"additionalProperties":false,"properties":{"kept":{"maximum":2147483647,"minimum":-2147483648,"type":"integer"}},"required":[],"type":"object"})");

static_assert(scrydantic::schema_v<std::uint64_t> ==
              R"({"maximum":18446744073709551615,"minimum":0,"type":"integer"})");
static_assert(
    scrydantic::schema_v<std::int64_t> ==
    R"({"maximum":9223372036854775807,"minimum":-9223372036854775808,"type":"integer"})");
static_assert(scrydantic::schema_v<std::int16_t> ==
              R"({"maximum":32767,"minimum":-32768,"type":"integer"})");

// ---- Runtime -----------------------------------------------------------------

TEST_CASE("tagged variants encode the tag at its canonical position") {
  const Drawing drawing{
      .shapes = {Circle{.radius = 1.5}, Rectangle{.height = 2, .width = 3}},
      .highlight = Shape{Rectangle{.height = 4, .width = 5}},
  };
  const auto expected = std::string{
      R"({"highlight":{"height":4,"type":"rectangle","width":5},)"
      R"("shapes":[{"radius":1.5,"type":"circle"},{"height":2,"type":"rectangle","width":3}]})"};

  const auto direct = scrydantic::detail::encode_value(drawing);
  REQUIRE(direct);
  CHECK(direct->text == expected);
  const auto canonical = scrydantic::encode(drawing);
  REQUIRE(canonical);
  CHECK(canonical->text == expected);

  CHECK(scrydantic::encode(Edge{EmptyAlternative{}})->text == R"({"type":"empty"})");
  CHECK(scrydantic::encode(Edge{LateAlternative{.zulu = 1}})->text ==
        R"({"type":"late","zulu":1})");
}

TEST_CASE("tagged variants decode by tag and round trip") {
  const auto text =
      std::string{R"({"highlight":null,"shapes":[{"radius":1.5,"type":"circle"},)"
                  R"({"height":2,"type":"rectangle","width":3}]})"};
  const auto decoded = decode_text<Drawing>(text);
  REQUIRE(decoded);
  REQUIRE(decoded->shapes.size() == 2);
  REQUIRE(std::holds_alternative<Circle>(decoded->shapes[0]));
  CHECK(std::get<Circle>(decoded->shapes[0]).radius == 1.5);
  REQUIRE(std::holds_alternative<Rectangle>(decoded->shapes[1]));
  CHECK(std::get<Rectangle>(decoded->shapes[1]).width == 3.0);
  CHECK_FALSE(decoded->highlight.has_value());
  CHECK(scrydantic::encode(*decoded)->text == text);

  const auto empty = decode_text<Edge>(R"({"type":"empty"})");
  REQUIRE(empty);
  CHECK(std::holds_alternative<EmptyAlternative>(*empty));
}

TEST_CASE("tagged variant decode failures list the declared tags") {
  const auto failure = [](const std::string_view text) {
    const auto decoded = decode_text<Drawing>(text);
    REQUIRE_FALSE(decoded);
    CHECK(decoded.error().message == decoded.error().model_message);
    return decoded.error().model_message;
  };

  CHECK(failure(R"({"shapes":[{"radius":1}]})") ==
        "$.shapes[0].type is a required member; must be one of: circle, rectangle");
  CHECK(failure(R"({"shapes":[{"type":"hexagon"}]})") ==
        "$.shapes[0].type must be one of: circle, rectangle");
  CHECK(failure(R"({"shapes":[{"type":7}]})") ==
        "$.shapes[0].type must be one of: circle, rectangle");
  CHECK(failure(R"({"shapes":[3]})") == "$.shapes[0] must be an object");
  CHECK(failure(R"({"shapes":[{"radius":"big","type":"circle"}]})") ==
        "$.shapes[0].radius must be a number");
  CHECK(failure(R"({"shapes":[{"radius":1,"side":2,"type":"circle"}]})") ==
        R"($.shapes[0] contains unknown member "side")");
  CHECK(failure(R"({"highlight":{"type":"square"},"shapes":[]})") ==
        "$.highlight.type must be one of: circle, rectangle");
}

TEST_CASE("a tagged class outside a variant neither writes nor accepts its tag") {
  CHECK(scrydantic::encode(Left{.value = 1})->text == R"({"value":1})");
  const auto decoded = decode_text<Left>(R"({"type":"left","value":1})");
  REQUIRE_FALSE(decoded);
  CHECK(decoded.error().model_message == R"($ contains unknown member "type")");
}

TEST_CASE("name annotations replace keys in encoding decoding and errors") {
  const Renamed value{.limit = 5, .zed = "z"};
  CHECK(scrydantic::encode(value)->text == R"({"a_first":"z","max_tokens":5})");

  const auto decoded = decode_text<Renamed>(R"({"a_first":"y","max_tokens":7})");
  REQUIRE(decoded);
  CHECK(decoded->limit == 7U);
  CHECK(decoded->zed == "y");

  auto rejected = decode_text<Renamed>(R"({"a_first":"y","limit":7})");
  REQUIRE_FALSE(rejected);
  CHECK(rejected.error().model_message == R"($ contains unknown member "limit")");

  rejected = decode_text<Renamed>(R"({"a_first":"y","max_tokens":-1})");
  REQUIRE_FALSE(rejected);
  CHECK(rejected.error().message == "$.max_tokens is outside the integer range");

  rejected = decode_text<Renamed>(R"({"max_tokens":1})");
  REQUIRE_FALSE(rejected);
  CHECK(rejected.error().model_message == "$.a_first is a required member");

  // With several bad members, each is reported, in declaration order, even
  // though the first one's key sorts later.
  rejected = decode_text<Renamed>(R"({"a_first":1,"max_tokens":"x"})");
  REQUIRE_FALSE(rejected);
  CHECK(rejected.error().model_message ==
        "$.max_tokens must be an integer\n$.a_first must be a string");
}

TEST_CASE("skip_null omits disengaged members and reads their absence as null") {
  CHECK(scrydantic::encode(Sparse{.absent = std::nullopt,
                                  .explicit_null = std::nullopt,
                                  .initialized = std::nullopt})
            ->text == R"({"explicit_null":null,"plain":0})");
  CHECK(scrydantic::encode(Sparse{.absent = 1, .explicit_null = 2, .initialized = 3})
            ->text == R"({"absent":1,"explicit_null":2,"initialized":3,"plain":0})");

  // Absence overrides the initializer, so an encoded value decodes to itself.
  const auto decoded = decode_text<Sparse>(R"({"explicit_null":null})");
  REQUIRE(decoded);
  CHECK_FALSE(decoded->absent.has_value());
  CHECK_FALSE(decoded->explicit_null.has_value());
  CHECK_FALSE(decoded->initialized.has_value());
  CHECK(decoded->plain == 0);

  // An explicit null is still accepted where the member may be omitted.
  const auto explicit_null =
      decode_text<Sparse>(R"({"absent":null,"explicit_null":1})");
  REQUIRE(explicit_null);
  CHECK_FALSE(explicit_null->absent.has_value());
  CHECK(explicit_null->explicit_null == 1);

  const auto missing = decode_text<Sparse>("{}");
  REQUIRE_FALSE(missing);
  CHECK(missing.error().model_message == "$.explicit_null is a required member");

  CHECK(scrydantic::encode(MemberSkip{.note = std::nullopt, .other = std::nullopt})
            ->text == R"({"other":null})");
  CHECK(scrydantic::encode(MemberSkip{.note = "n", .other = std::nullopt})->text ==
        R"({"note":"n","other":null})");
  const auto member = decode_text<MemberSkip>(R"({"other":"o"})");
  REQUIRE(member);
  CHECK_FALSE(member->note.has_value());
  CHECK(member->other == "o");
}

TEST_CASE("ignore_unknown decodes leniently at its own level only") {
  const auto lenient = decode_text<LenientEvent>(
      R"({"inner":{"extra":1,"kept":2},"kind":"k","other":[1,2]})");
  REQUIRE(lenient);
  CHECK(lenient->inner.kept == 2);
  CHECK(lenient->kind == "k");

  // Unknown members are ignored; the declared ones are as strict as ever.
  auto rejected = decode_text<LenientEvent>(R"({"inner":{"kept":"two"},"kind":"k"})");
  REQUIRE_FALSE(rejected);
  CHECK(rejected.error().model_message == "$.inner.kept must be an integer");
  rejected = decode_text<LenientEvent>(R"({"inner":{}})");
  REQUIRE_FALSE(rejected);
  CHECK(rejected.error().model_message == "$.kind is a required member");

  // A strict class around a lenient one stays strict.
  CHECK(decode_text<StrictHolder>(R"({"inner":{"extra":1,"kept":2}})"));
  const auto strict = decode_text<StrictHolder>(R"({"inner":{"kept":2},"surprise":1})");
  REQUIRE_FALSE(strict);
  CHECK(strict.error().model_message == R"($ contains unknown member "surprise")");

  // Leniency is per alternative, and never swallows the tag.
  const auto delta =
      decode_text<StreamEvent>(R"({"index":0,"text":"hi","type":"delta"})");
  REQUIRE(delta);
  CHECK(std::get<LenientDelta>(*delta).text == "hi");
  const auto stop = decode_text<StreamEvent>(R"({"index":0,"type":"stop"})");
  REQUIRE_FALSE(stop);
  CHECK(stop.error().model_message == R"($ contains unknown member "index")");
}

TEST_CASE("encode-only values borrow text and splice JSON verbatim") {
  const std::string model = "model-name";
  const BorrowedRequest body{
      .max_tokens = 1024,
      .model = model,
      .temperature = 0.7,
      .tools = std::vector<BorrowedTool>{{
          .description = "Tab\there",
          .input_schema = {.text = R"({"type":"object", "properties":{}})"},
          .name = "lookup",
      }},
  };

  const auto direct = scrydantic::detail::encode_value(body);
  REQUIRE(direct);
  CHECK(direct->text ==
        R"({"max_tokens":1024,"model":"model-name","stream":true,"temperature":0.7,)"
        R"("tools":[{"description":"Tab\there","input_schema":{"type":"object", )"
        R"("properties":{}},"name":"lookup"}]})");

  const auto canonical = scrydantic::encode(body);
  REQUIRE(canonical);
  CHECK(canonical->text ==
        R"({"max_tokens":1024,"model":"model-name","stream":true,"temperature":0.7,)"
        R"("tools":[{"description":"Tab\there","input_schema":{"properties":{},)"
        R"("type":"object"},"name":"lookup"}]})");

  CHECK(scrydantic::encode(BorrowedRequest{.model = "m", .system = "s", .top_p = 0.5})
            ->text ==
        R"({"model":"m","stream":true,"system":"s","temperature":0,"top_p":0.5})");
}

// ---- Canonical spelling ------------------------------------------------------

namespace {

// encode() writes canonical text directly. It must write exactly the bytes that
// canonicalizing the verbatim writer's text writes, so the two spellings agree.
template <typename Type>
[[nodiscard]] std::optional<std::string> spelling_mismatch(const Type& value) {
  const auto direct = scrydantic::encode(value);
  const auto verbatim = scrydantic::detail::encode_value(value);
  if (!direct || !verbatim) {
    return direct ? "the verbatim writer failed" : "encode() failed";
  }
  const auto parsed = scrydantic::JsonView::parse(*verbatim);
  const auto canonical = parsed ? parsed->to_json().text : "nothing";
  if (canonical != direct->text) {
    return "verbatim " + verbatim->text + " canonicalizes to " + canonical +
           " but encode() wrote " + direct->text;
  }
  return std::nullopt;
}

template <typename Float>
[[nodiscard]] std::vector<std::string>
float_spelling_mismatches(const std::vector<Float>& values) {
  std::vector<std::string> mismatches{};
  for (const Float value : values) {
    if (const auto mismatch = spelling_mismatch(value)) {
      mismatches.push_back(*mismatch);
    }
  }
  return mismatches;
}

// Values at every boundary the spelling depends on: signed zeros, subnormals,
// every power of two and of ten, the edges of the positional range, and the
// integers around 2^53, 2^63, and 2^64, where the text of a whole double may or
// may not read back as an integer; then deterministic random bit patterns and
// random decimals.
template <typename Float> [[nodiscard]] std::vector<Float> spelling_cases() {
  using limits = std::numeric_limits<Float>;
  using Bits = std::conditional_t<sizeof(Float) == 8, std::uint64_t, std::uint32_t>;
  std::vector<Float> values{Float{0},      -Float{0},          limits::denorm_min(),
                            limits::min(), limits::max(),      limits::epsilon(),
                            Float{1},      Float{1} / Float{3}};
  for (const double value :
       {0.1, 123.456, 1e15, 1e16, 9.999999e15, 1e-4, 1e-5, 0.000123}) {
    values.push_back(static_cast<Float>(value));
  }
  for (int exponent = limits::min_exponent - limits::digits;
       exponent < limits::max_exponent; ++exponent) {
    values.push_back(std::ldexp(Float{1}, exponent));
  }
  for (int exponent = limits::min_exponent10 - limits::digits10;
       exponent <= limits::max_exponent10; ++exponent) {
    const auto text = "1e" + std::to_string(exponent);
    Float value{};
    if (std::from_chars(text.data(), text.data() + text.size(), value).ec ==
        std::errc{}) {
      values.push_back(value);
    }
  }
  for (const int exponent :
       {limits::digits - 1, limits::digits, limits::digits + 1, 62, 63, 64, 65}) {
    const auto whole = std::ldexp(Float{1}, exponent);
    for (int step = -4; step <= 4; ++step) {
      values.push_back(std::nextafter(whole, step < 0 ? Float{0} : limits::infinity()));
      values.push_back(whole + static_cast<Float>(step));
    }
  }
  values.push_back(static_cast<Float>(123456789012345680.0));
  values.push_back(static_cast<Float>(-9223372036854775808.0));
  values.push_back(static_cast<Float>(18446744073709551616.0));

  std::mt19937_64 generator{20261103};
  for (int sample = 0; sample < 20000; ++sample) {
    const auto value = std::bit_cast<Float>(static_cast<Bits>(generator()));
    if (std::isfinite(value)) {
      values.push_back(value);
    }
  }
  std::uniform_int_distribution<std::int64_t> mantissa{-99999999, 99999999};
  std::uniform_int_distribution<int> decimal_exponent{-30, 30};
  for (int sample = 0; sample < 20000; ++sample) {
    const auto text = std::to_string(mantissa(generator)) + "e" +
                      std::to_string(decimal_exponent(generator));
    Float value{};
    std::from_chars(text.data(), text.data() + text.size(), value);
    values.push_back(value);
  }
  for (const auto value : std::vector<Float>{values}) {
    values.push_back(-value);
  }
  return values;
}

} // namespace

TEST_CASE("encode() spells doubles as canonicalizing the verbatim text would") {
  const auto mismatches = float_spelling_mismatches(spelling_cases<double>());
  for (std::size_t index = 0; index < mismatches.size() && index < 5; ++index) {
    UNSCOPED_INFO(mismatches[index]);
  }
  CHECK(mismatches.empty());
}

TEST_CASE("encode() spells floats as canonicalizing the verbatim text would") {
  const auto mismatches = float_spelling_mismatches(spelling_cases<float>());
  for (std::size_t index = 0; index < mismatches.size() && index < 5; ++index) {
    UNSCOPED_INFO(mismatches[index]);
  }
  CHECK(mismatches.empty());
}

TEST_CASE("encode() canonicalizes spliced payloads as a whole-text pass would") {
  for (const std::string_view payload : {
           R"( { "b" : [ 1.0 , -0 , 1e2 , 2.50 ] , "a" : null , "b" : true } )",
           R"({"Aé😀":"\/\u001f\b\t"})",
           "18446744073709551615",
           "-9223372036854775808",
           "1E400",
           R"("text")",
       }) {
    INFO(payload);
    const Payload value{.body = {.text = std::string{payload}}};
    if (!scrydantic::detail::validate_json(payload)) {
      CHECK_FALSE(scrydantic::encode(value));
      continue;
    }
    CHECK(spelling_mismatch(value) == std::nullopt);
  }
  CHECK(
      spelling_mismatch(BorrowedRequest{
          .max_tokens = 7,
          .model = "m\x7f\"\\\n",
          .temperature = 1e-7,
          .tools =
              std::vector<BorrowedTool>{{.input_schema = {.text = R"({"z":1,"a":2})"}}},
          .top_p = 0.1}) == std::nullopt);
}

TEST_CASE("strings are escaped as the canonical writer escapes them") {
  // Every byte JSON requires escaping, then DEL and multi-byte UTF-8, which pass
  // through: the codec's run-time writer and the core's agree byte for byte.
  const std::string text = "\"\\/\b\f\n\r\t\x01\x1f\x7f\xc3\xa9";
  const auto expected =
      std::string{R"("\"\\/\b\f\n\r\t\u0001\u001F)"} + "\x7f\xc3\xa9\"";
  CHECK(scrydantic::encode(text)->text == expected);
  CHECK(scrydantic::detail::encode_value(text)->text == expected);
  CHECK(scrydantic::escape_json_string(text) == expected);
  CHECK(decode_text<std::string>(expected).value() == text);
}

TEST_CASE("encode() rejects a string that is not UTF-8") {
  const auto encoded = scrydantic::encode(DocText{.text = "bad \xff byte"});
  REQUIRE_FALSE(encoded);
  CHECK(encoded.error().message ==
        "the value holds a string that is not valid UTF-8, so it cannot be JSON");
  CHECK(encoded.error().model_message.empty());
}

TEST_CASE("encode() reports spliced payloads that nest too deep together") {
  // Each payload is valid alone; the enclosing object takes the whole text one
  // level past the parser's limit, and the text is ASCII throughout.
  const auto nest = [](const std::size_t depth) {
    return scrydantic::Json{.text = std::string(depth, '[') + "0" +
                                    std::string(depth, ']')};
  };
  constexpr std::string_view reason =
      "the value nests arrays and objects more than 256 deep, so it cannot be JSON";
  const DocToolResult deep{.result = nest(256), .tool_call_id = "c"};
  REQUIRE(scrydantic::JsonView::parse(deep.result));

  const auto encoded = scrydantic::encode(deep);
  REQUIRE_FALSE(encoded);
  CHECK(encoded.error().message == reason);
  CHECK(encoded.error().model_message.empty());

  const auto verbatim = scrydantic::detail::encode_value(deep).and_then(
      scrydantic::detail::validate_encoded_json);
  REQUIRE_FALSE(verbatim);
  CHECK(verbatim.error().message == reason);

  CHECK(scrydantic::encode(DocToolResult{.result = nest(255), .tool_call_id = "c"}));
}

TEST_CASE("encode() takes a reference or cv-qualified type argument") {
  // As a generic wrapper forwarding its own T writes it: the value type decides.
  std::optional<int> value{42};
  static_assert(scrydantic::Encodable<std::optional<int>&>);
  CHECK(scrydantic::encode<std::optional<int>&>(value)->text == "42");
  CHECK(scrydantic::encode<const std::optional<int>&>(value)->text == "42");
  CHECK(scrydantic::encode<const std::optional<int>>(value)->text == "42");
  const DocText text{.text = "hi"};
  CHECK(scrydantic::encode<const DocText&>(text)->text == R"({"text":"hi"})");
  CHECK(scrydantic::detail::encode_value<std::optional<int>&>(value)->text == "42");
}

TEST_CASE("an invalid spliced JSON payload fails encoding at its path") {
  for (const auto* text : {"", "   ", "{", R"({"a":1} 2)", "nul"}) {
    const BorrowedRequest body{
        .model = "m",
        .tools = std::vector<BorrowedTool>{{.input_schema = {.text = text}}},
    };
    const auto encoded = scrydantic::detail::encode_value(body);
    REQUIRE_FALSE(encoded);
    CHECK(encoded.error().message == "$.tools[0].input_schema is not valid JSON");
    CHECK(encoded.error().model_message.empty());
  }
}

TEST_CASE("document-like values round trip byte for byte") {
  const auto text = std::string{
      R"({"messages":[{"content":[{"text":"hello","type":"text"}],"role":"user"},)"
      R"({"content":[{"text":"calling","type":"text"},{"arguments":{"city":"Oslo",)"
      R"("days":[1,2]},"id":"call-1","name":"forecast","type":"tool_call"}],)"
      R"("role":"assistant"},{"content":[{"is_error":false,"result":{"summary":)"
      R"("sunny","temperature_c":21.5},"tool_call_id":"call-1","type":"tool_result"}],)"
      R"("role":"user"}],"system_prompt":"Be brief.","version":1})"};

  const auto decoded = decode_text<Document>(text);
  REQUIRE(decoded);
  REQUIRE(decoded->messages.size() == 3);
  CHECK(decoded->messages[1].role == Role::assistant);
  const auto& call = std::get<DocToolCall>(decoded->messages[1].content[1]);
  CHECK(call.arguments.text == R"({"city":"Oslo","days":[1,2]})");
  const auto& result = std::get<DocToolResult>(decoded->messages[2].content[0]);
  CHECK(result.result.text == R"({"summary":"sunny","temperature_c":21.5})");

  const auto direct = scrydantic::detail::encode_value(*decoded);
  REQUIRE(direct);
  CHECK(direct->text == text);
  const auto canonical = scrydantic::encode(*decoded);
  REQUIRE(canonical);
  CHECK(canonical->text == text);
}

TEST_CASE("decoding a JSON member captures the canonical text of any value") {
  const auto decoded = decode_text<Payload>(R"({"body":{"b":[1, 2.50],"a":null}})");
  REQUIRE(decoded);
  CHECK(decoded->body.text == R"({"a":null,"b":[1,2.5]})");

  CHECK(decode_text<Payload>(R"({"body":null})")->body.text == "null");
  CHECK_FALSE(decode_text<Payload>("{}"));
  CHECK(scrydantic::encode(ConstView{.value = 4})->text == R"({"value":4})");
  CHECK(decode_text<Payload>(R"({"body":"text"})")->body.text == R"("text")");
  CHECK(decode_text<scrydantic::Json>("[true]")->text == "[true]");
  CHECK_FALSE(decode_text<std::optional<scrydantic::Json>>("null")->has_value());
  CHECK(decode_text<std::optional<scrydantic::Json>>("{}")->value().text == "{}");
}

TEST_CASE("JsonView serializes the viewed node canonically") {
  const auto parsed = scrydantic::JsonView::parse(
      scrydantic::Json{.text = R"({"z":[1, 2],"a":{"d":null,"c":"x"}})"});
  REQUIRE(parsed);
  CHECK(parsed->to_json().text == R"({"a":{"c":"x","d":null},"z":[1,2]})");
  CHECK(parsed->find("a")->to_json().text == R"({"c":"x","d":null})");
  CHECK(parsed->find("z")->at(1)->to_json().text == "2");
  CHECK(scrydantic::JsonView{}.to_json().text == "null");
}

TEST_CASE("public decoding reports path-based messages") {
  auto decoded = decode_text<Renamed>("{");
  REQUIRE_FALSE(decoded);
  CHECK(decoded.error().message == "JSON text is not valid JSON");
  CHECK(decoded.error().model_message == "JSON text is not valid JSON");

  decoded = decode_text<Renamed>("[]");
  REQUIRE_FALSE(decoded);
  CHECK(decoded.error().message == "$ must be an object");
  CHECK(decoded.error().model_message == "$ must be an object");

  const auto view =
      scrydantic::JsonView::parse(scrydantic::Json{.text = R"({"a_first":"v"})"});
  REQUIRE(view);
  const auto from_view = scrydantic::decode<Renamed>(*view);
  REQUIRE(from_view);
  CHECK(from_view->zed == "v");
  CHECK(from_view->limit == 0U);

  CHECK(decode_text<std::vector<Role>>(R"(["user","assistant"])").value() ==
        std::vector<Role>{Role::user, Role::assistant});
  const auto role = decode_text<Role>(R"("system")");
  REQUIRE_FALSE(role);
  CHECK(role.error().model_message ==
        "$ is not a declared enumerator; must be one of: user, assistant");
}

TEST_CASE("encoding reports a valueless variant instead of throwing") {
  struct ThrowingText {
    // NOLINTNEXTLINE(google-explicit-constructor): the conversion is the point.
    operator std::string() const { throw 1; }
  };
  DocBlock block{DocText{.text = "before"}};
  try {
    block.emplace<DocText>(ThrowingText{});
  } catch (const int) {
  }
  REQUIRE(block.valueless_by_exception());

  const auto encoded = scrydantic::encode(block);
  REQUIRE_FALSE(encoded);
  CHECK(encoded.error().message == "$ is a variant left valueless by an exception");
}

// ---- Decode issues -----------------------------------------------------------

namespace {

struct Account {
  std::string name;
  std::int16_t age{};
  bool active{};
  Role role{};
};

struct Address {
  std::string city;
  std::uint32_t zip{};
};

struct Customer {
  Address home;
  std::optional<Address> work = std::nullopt;
  std::vector<std::int32_t> scores;
  std::array<bool, 2> flags{};
};

// Keys that a path cannot spell in dot shorthand.
struct Awkward {
  [[= scrydantic::name{"content-type"}]] std::string content_type;
  [[= scrydantic::name{"a.b"}]] std::vector<int> dotted;
  [[= scrydantic::name{"\"]"}]] bool quote{};
};

struct AwkwardNumber {
  [[= scrydantic::name{"a.b"}]] double value{};
};

struct Bulk {
  std::vector<int> values;
  std::string after;
};

using scrydantic::IssueKind;
using Issues = std::vector<scrydantic::Issue>;

constexpr std::string_view stopped_line =
    "further issues past the first 64 were not reported";

// A JSON array of `count` strings, each an issue where Bulk wants integers.
[[nodiscard]] std::string wrong_values(const std::size_t count) {
  std::string text{"["};
  for (std::size_t index = 0; index < count; ++index) {
    text.append(index == 0 ? R"("x")" : R"(,"x")");
  }
  text.push_back(']');
  return text;
}

// The message lines of the first `count` wrong values, joined.
[[nodiscard]] std::string wrong_value_lines(const std::size_t count) {
  std::string text{};
  for (std::size_t index = 0; index < count; ++index) {
    text.append(index == 0 ? "" : "\n")
        .append("$.values[" + std::to_string(index) + "] must be an integer");
  }
  return text;
}

} // namespace

namespace {

// A function template because GCC 16 reports -Wshadow for each expansion of a
// `template for` in a function that is not one.
template <typename Enum> [[nodiscard]] std::string enumerator_name(const Enum value) {
  template for (constexpr std::meta::info enumerator :
                std::define_static_array(std::meta::enumerators_of(^^Enum))) {
    if (value == [:enumerator:]) {
      return std::string{std::meta::identifier_of(enumerator)};
    }
  }
  return "an undeclared enumerator";
}

} // namespace

// Issues print as `kind at path: reason`, so a failed comparison shows them.
template <> struct Catch::StringMaker<scrydantic::IssueKind> {
  static std::string convert(const scrydantic::IssueKind kind) {
    return enumerator_name(kind);
  }
};

template <> struct Catch::StringMaker<scrydantic::Issue> {
  static std::string convert(const scrydantic::Issue& issue) {
    return StringMaker<scrydantic::IssueKind>::convert(issue.kind) + " at " +
           scrydantic::json_path(issue.path) + ": " + issue.reason;
  }
};

TEST_CASE("decoding reports every issue in an object, in declaration order") {
  const auto decoded = decode_text<Account>(
      R"({"active":"yes","age":70000,"extra":1,"name":7,"role":"admin"})");
  REQUIRE_FALSE(decoded);
  // Unknown members are found before the declared ones are read.
  CHECK(decoded.error().issues ==
        Issues{
            {.kind = IssueKind::unknown_member,
             .reason = R"(contains unknown member "extra")"},
            {.kind = IssueKind::wrong_type,
             .path = {"name"},
             .reason = "must be a string"},
            {.kind = IssueKind::out_of_range,
             .path = {"age"},
             .reason = "is outside the integer range"},
            {.kind = IssueKind::wrong_type,
             .path = {"active"},
             .reason = "must be a boolean"},
            {.kind = IssueKind::unknown_enumerator,
             .path = {"role"},
             .reason = "is not a declared enumerator; must be one of: user, assistant"},
        });
  constexpr std::string_view message =
      "$ contains unknown member \"extra\"\n"
      "$.name must be a string\n"
      "$.age is outside the integer range\n"
      "$.active must be a boolean\n"
      "$.role is not a declared enumerator; must be one of: user, assistant";
  CHECK(decoded.error().message == message);
  CHECK(decoded.error().model_message == message);

  // Each unknown member is its own issue, in key order, and a missing required
  // member is one more.
  const auto sparse = decode_text<Account>(R"({"b":1,"a":2})");
  REQUIRE_FALSE(sparse);
  CHECK(sparse.error().issues == Issues{
                                     {.kind = IssueKind::unknown_member,
                                      .reason = R"(contains unknown member "a")"},
                                     {.kind = IssueKind::unknown_member,
                                      .reason = R"(contains unknown member "b")"},
                                     {.kind = IssueKind::missing_member,
                                      .path = {"name"},
                                      .reason = "is a required member"},
                                 });
  CHECK(sparse.error().model_message == "$ contains unknown member \"a\"\n"
                                        "$ contains unknown member \"b\"\n"
                                        "$.name is a required member");
}

TEST_CASE("issues inside nested objects, optionals, and arrays carry full paths") {
  const auto decoded = decode_text<Customer>(
      R"({"flags":[true,1],"home":{"zip":-1},"scores":[1,"two",3,4.5],)"
      R"("work":{"city":null,"zip":"x"}})");
  REQUIRE_FALSE(decoded);
  CHECK(decoded.error().issues == Issues{
                                      {.kind = IssueKind::missing_member,
                                       .path = {"home", "city"},
                                       .reason = "is a required member"},
                                      {.kind = IssueKind::out_of_range,
                                       .path = {"home", "zip"},
                                       .reason = "is outside the integer range"},
                                      {.kind = IssueKind::wrong_type,
                                       .path = {"work", "city"},
                                       .reason = "must be a string"},
                                      {.kind = IssueKind::wrong_type,
                                       .path = {"work", "zip"},
                                       .reason = "must be an integer"},
                                      {.kind = IssueKind::wrong_type,
                                       .path = {"scores", 1UZ},
                                       .reason = "must be an integer"},
                                      {.kind = IssueKind::wrong_type,
                                       .path = {"scores", 3UZ},
                                       .reason = "must be an integer"},
                                      {.kind = IssueKind::wrong_type,
                                       .path = {"flags", 1UZ},
                                       .reason = "must be a boolean"},
                                  });
  constexpr std::string_view message = "$.home.city is a required member\n"
                                       "$.home.zip is outside the integer range\n"
                                       "$.work.city must be a string\n"
                                       "$.work.zip must be an integer\n"
                                       "$.scores[1] must be an integer\n"
                                       "$.scores[3] must be an integer\n"
                                       "$.flags[1] must be a boolean";
  CHECK(decoded.error().message == message);
  CHECK(decoded.error().model_message == message);
}

TEST_CASE("a value of the wrong JSON type is one issue, and nothing inside is read") {
  const auto decoded =
      decode_text<Customer>(R"({"flags":[true],"home":[],"scores":{},"work":"w"})");
  REQUIRE_FALSE(decoded);
  CHECK(decoded.error().issues ==
        Issues{
            {.kind = IssueKind::wrong_type,
             .path = {"home"},
             .reason = "must be an object"},
            {.kind = IssueKind::wrong_type,
             .path = {"work"},
             .reason = "must be an object"},
            {.kind = IssueKind::wrong_type,
             .path = {"scores"},
             .reason = "must be an array"},
            {.kind = IssueKind::wrong_size,
             .path = {"flags"},
             .reason = "must be an array of the declared fixed size"},
        });
  CHECK(decoded.error().model_message == "$.home must be an object\n"
                                         "$.work must be an object\n"
                                         "$.scores must be an array\n"
                                         "$.flags must be an array of the declared "
                                         "fixed size");

  // A fixed-size array's two failures share their text but not their kind.
  const auto not_array =
      decode_text<Customer>(R"({"flags":{},"home":{"city":""},"scores":[]})");
  REQUIRE_FALSE(not_array);
  CHECK(not_array.error().issues ==
        Issues{{.kind = IssueKind::wrong_type,
                .path = {"flags"},
                .reason = "must be an array of the declared fixed size"}});
  CHECK(not_array.error().model_message ==
        "$.flags must be an array of the declared fixed size");
}

TEST_CASE("a variant reports its alternative's issues, or one issue for its tag") {
  const auto decoded = decode_text<Drawing>(
      R"({"highlight":{"radius":"big"},"shapes":[)"
      R"({"radius":"big","side":1,"type":"circle"},{"type":"hexagon","width":"w"},7,)"
      R"({"height":"h","type":"rectangle","width":"w"}]})");
  REQUIRE_FALSE(decoded);
  // With no alternative chosen, an object's other members are not checked: the
  // hexagon's width and the highlight's radius go unreported.
  CHECK(decoded.error().issues ==
        Issues{
            {.kind = IssueKind::unknown_member,
             .path = {"shapes", 0UZ},
             .reason = R"(contains unknown member "side")"},
            {.kind = IssueKind::wrong_type,
             .path = {"shapes", 0UZ, "radius"},
             .reason = "must be a number"},
            {.kind = IssueKind::unknown_tag,
             .path = {"shapes", 1UZ, "type"},
             .reason = "must be one of: circle, rectangle"},
            {.kind = IssueKind::wrong_type,
             .path = {"shapes", 2UZ},
             .reason = "must be an object"},
            {.kind = IssueKind::wrong_type,
             .path = {"shapes", 3UZ, "height"},
             .reason = "must be a number"},
            {.kind = IssueKind::wrong_type,
             .path = {"shapes", 3UZ, "width"},
             .reason = "must be a number"},
            {.kind = IssueKind::missing_tag,
             .path = {"highlight", "type"},
             .reason = "is a required member; must be one of: circle, rectangle"},
        });
  constexpr std::string_view message =
      "$.shapes[0] contains unknown member \"side\"\n"
      "$.shapes[0].radius must be a number\n"
      "$.shapes[1].type must be one of: circle, rectangle\n"
      "$.shapes[2] must be an object\n"
      "$.shapes[3].height must be a number\n"
      "$.shapes[3].width must be a number\n"
      "$.highlight.type is a required member; must be one of: circle, rectangle";
  CHECK(decoded.error().message == message);
  CHECK(decoded.error().model_message == message);

  // A tag that is not a string names no alternative either.
  const auto untyped = decode_text<Shape>(R"({"radius":"big","type":7})");
  REQUIRE_FALSE(untyped);
  CHECK(untyped.error().issues ==
        Issues{{.kind = IssueKind::unknown_tag,
                .path = {"type"},
                .reason = "must be one of: circle, rectangle"}});
}

TEST_CASE("text that is not JSON is one issue at the root") {
  const auto decoded = decode_text<Account>(R"({"name":)");
  REQUIRE_FALSE(decoded);
  CHECK(decoded.error().issues ==
        Issues{{.kind = IssueKind::invalid_json, .reason = "is not valid JSON"}});
  CHECK(decoded.error().message == "JSON text is not valid JSON");
  CHECK(decoded.error().model_message == "JSON text is not valid JSON");
}

TEST_CASE("paths quote keys that are not identifiers") {
  const auto decoded =
      decode_text<Awkward>(R"({"\"]":0,"a.b":[1,"x"],"content-type":1,"x.y":2})");
  REQUIRE_FALSE(decoded);
  CHECK(decoded.error().issues == Issues{
                                      {.kind = IssueKind::unknown_member,
                                       .reason = R"(contains unknown member "x.y")"},
                                      {.kind = IssueKind::wrong_type,
                                       .path = {"content-type"},
                                       .reason = "must be a string"},
                                      {.kind = IssueKind::wrong_type,
                                       .path = {"a.b", 1UZ},
                                       .reason = "must be an integer"},
                                      {.kind = IssueKind::wrong_type,
                                       .path = {"\"]"},
                                       .reason = "must be a boolean"},
                                  });
  CHECK(decoded.error().model_message == R"($ contains unknown member "x.y")"
                                         "\n"
                                         R"($["content-type"] must be a string)"
                                         "\n"
                                         R"($["a.b"][1] must be an integer)"
                                         "\n"
                                         R"($["\"]"] must be a boolean)");

  // An encode failure spells its path the same way, and is no decode issue.
  const auto encoded = scrydantic::encode(
      AwkwardNumber{.value = std::numeric_limits<double>::infinity()});
  REQUIRE_FALSE(encoded);
  CHECK(encoded.error().message == R"($["a.b"] must be finite)");
  CHECK(encoded.error().model_message.empty());
  CHECK(encoded.error().issues.empty());
}

TEST_CASE("decoding reports at most max_issues issues, then says it stopped") {
  static_assert(scrydantic::max_issues == 64);
  const auto check_reported = [](const std::size_t count) {
    INFO(count << " wrong values");
    const auto decoded =
        decode_text<Bulk>(R"({"after":"","values":)" + wrong_values(count) + "}");
    REQUIRE_FALSE(decoded);
    const auto& error = decoded.error();
    const std::size_t reported = std::min(count, scrydantic::max_issues);
    REQUIRE(error.issues.size() == reported);
    for (std::size_t index = 0; index < reported; ++index) {
      CHECK(error.issues[index] == scrydantic::Issue{.kind = IssueKind::wrong_type,
                                                     .path = {"values", index},
                                                     .reason = "must be an integer"});
    }
    const auto expected =
        count > scrydantic::max_issues
            ? wrong_value_lines(reported) + "\n" + std::string{stopped_line}
            : wrong_value_lines(reported);
    CHECK(error.message == expected);
    CHECK(error.model_message == expected);
    return error;
  };
  check_reported(scrydantic::max_issues - 1);
  check_reported(scrydantic::max_issues);
  const auto stopped = check_reported(scrydantic::max_issues + 1);
  // Hostile input reports exactly what one issue too many does.
  const auto hostile = check_reported(100'000);
  CHECK(hostile.issues == stopped.issues);
  CHECK(hostile.message == stopped.message);
}

TEST_CASE("an issue past max_issues stops the whole decode") {
  // Below the limit, the missing `after` is the 64th issue and is reported.
  const auto at_limit = decode_text<Bulk>(R"({"values":)" + wrong_values(63) + "}");
  REQUIRE_FALSE(at_limit);
  REQUIRE(at_limit.error().issues.size() == scrydantic::max_issues);
  CHECK(at_limit.error().issues.back() ==
        scrydantic::Issue{.kind = IssueKind::missing_member,
                          .path = {"after"},
                          .reason = "is a required member"});
  CHECK(at_limit.error().message ==
        wrong_value_lines(63) + "\n$.after is a required member");

  // At it, the missing `after` is the 65th, a sibling of the array that filled
  // the report, and the decode stops there.
  const auto past_limit = decode_text<Bulk>(R"({"values":)" + wrong_values(64) + "}");
  REQUIRE_FALSE(past_limit);
  CHECK(past_limit.error().issues.size() == scrydantic::max_issues);
  CHECK(past_limit.error().message ==
        wrong_value_lines(64) + "\n" + std::string{stopped_line});

  // Unknown members count too, and stop the decode before any declared member.
  // Three-digit keys visit in the order they are written.
  std::string unknown{"{"};
  for (std::size_t index = 0; index <= scrydantic::max_issues; ++index) {
    unknown.append(index == 0 ? "" : ",")
        .append("\"k" + std::to_string(100 + index) + "\":0");
  }
  unknown.push_back('}');
  const auto crowded = decode_text<Bulk>(unknown);
  REQUIRE_FALSE(crowded);
  REQUIRE(crowded.error().issues.size() == scrydantic::max_issues);
  CHECK(crowded.error().issues.back() ==
        scrydantic::Issue{.kind = IssueKind::unknown_member,
                          .reason = R"(contains unknown member "k163")"});
  CHECK(crowded.error().message.ends_with("\n$ contains unknown member \"k163\"\n" +
                                          std::string{stopped_line}));
}

// ---- Indirection -------------------------------------------------------------

namespace {

struct Boxed {
  std::optional<std::indirect<Shape>> highlight;
  std::indirect<Renamed> renamed;
};

struct Unboxed {
  std::optional<Shape> highlight;
  Renamed renamed;
};

struct OptionalInside {
  std::indirect<std::optional<int>> value;
};

// Decoding accepts any allocator it can default-construct, as for std::vector.
struct Pooled {
  std::pmr::indirect<int> value;
};

// An allocator that must be given the resource it allocates from.
template <typename Value> class BoundAllocator {
public:
  using value_type = Value;

  explicit BoundAllocator(std::pmr::memory_resource& resource) noexcept
      : resource_{&resource} {}
  template <typename Other>
  // NOLINTNEXTLINE(google-explicit-constructor): allocators rebind implicitly.
  BoundAllocator(const BoundAllocator<Other>& other) noexcept
      : resource_{other.resource()} {}

  [[nodiscard]] Value* allocate(const std::size_t count) {
    return static_cast<Value*>(
        resource_->allocate(count * sizeof(Value), alignof(Value)));
  }
  void deallocate(Value* const pointer, const std::size_t count) noexcept {
    resource_->deallocate(pointer, count * sizeof(Value), alignof(Value));
  }
  [[nodiscard]] std::pmr::memory_resource* resource() const noexcept {
    return resource_;
  }
  friend bool operator==(const BoundAllocator&, const BoundAllocator&) = default;

private:
  std::pmr::memory_resource* resource_;
};

struct Bound {
  std::indirect<int, BoundAllocator<int>> value;
};

} // namespace

static_assert(scrydantic::SupportedValue<Boxed>);
static_assert(!scrydantic::SupportedValue<OptionalInside>);
static_assert(
    !scrydantic::SupportedValue<std::optional<std::indirect<std::optional<int>>>>);
static_assert(scrydantic::SupportedValue<Pooled>);
// Encoding only reads, so it takes an allocator that decoding cannot construct.
static_assert(scrydantic::Encodable<Bound>);
static_assert(!scrydantic::Decodable<Bound>);

static_assert(mentions(scrydantic::detail::supported_diagnostic<OptionalInside>(),
                       "OptionalInside::value: std::indirect<std::optional<int> > "
                       "nests std::optional inside std::indirect; "
                       "std::optional<std::indirect<T>> is the nullable form"));
static_assert(mentions(scrydantic::detail::decodable_diagnostic<Bound>(),
                       "Bound::value: std::indirect<int, "
                       "{anonymous}::BoundAllocator<int> > must be default-"
                       "constructible, move-constructible, and move-assignable"));

// An indirect has the schema of the value it owns, and an optional one joins null
// to a variant's alternatives as an optional variant does.
static_assert(scrydantic::schema_v<Boxed> == scrydantic::schema_v<Unboxed>);
static_assert(
    scrydantic::schema_v<Pooled> ==
    R"({"additionalProperties":false,"properties":{"value":{"maximum":2147483647,"minimum":-2147483648,"type":"integer"}},"required":["value"],"type":"object"})");

TEST_CASE("std::indirect encodes and decodes as the value it owns") {
  const Boxed boxed{
      .highlight = std::indirect<Shape>(Shape{Circle{.radius = 1}}),
      .renamed = std::indirect<Renamed>(Renamed{.limit = 5, .zed = "z"}),
  };
  const Unboxed unboxed{
      .highlight = Shape{Circle{.radius = 1}},
      .renamed = Renamed{.limit = 5, .zed = "z"},
  };
  const auto text = std::string{R"({"highlight":{"radius":1,"type":"circle"},)"
                                R"("renamed":{"a_first":"z","max_tokens":5}})"};
  CHECK(scrydantic::encode(boxed)->text == text);
  CHECK(scrydantic::encode(unboxed)->text == text);

  const auto decoded = decode_text<Boxed>(text);
  REQUIRE(decoded);
  CHECK(std::get<Circle>(**decoded->highlight).radius == 1.0);
  CHECK(decoded->renamed->zed == "z");
  CHECK_FALSE(decode_text<Boxed>(R"({"highlight":null,"renamed":{"a_first":"y"}})")
                  ->highlight.has_value());

  const auto failed = decode_text<Boxed>(
      R"({"highlight":{"type":"hexagon"},"renamed":{"a_first":"y"}})");
  REQUIRE_FALSE(failed);
  CHECK(failed.error().model_message ==
        "$.highlight.type must be one of: circle, rectangle");

  const auto pooled = decode_text<Pooled>(R"({"value":7})");
  REQUIRE(pooled);
  CHECK(*pooled->value == 7);
  CHECK(scrydantic::encode(*pooled)->text == R"({"value":7})");

  std::pmr::monotonic_buffer_resource resource{};
  const Bound bound{.value = std::indirect<int, BoundAllocator<int>>(
                        std::allocator_arg, BoundAllocator<int>{resource}, 3)};
  CHECK(scrydantic::encode(bound)->text == R"({"value":3})");
}

TEST_CASE("encoding reports a std::indirect left valueless by a move") {
  Boxed boxed{
      .highlight = std::nullopt,
      .renamed = std::indirect<Renamed>(Renamed{.limit = 5, .zed = "z"}),
  };
  const auto taken = std::move(boxed.renamed);
  REQUIRE(boxed.renamed.valueless_after_move());

  const auto encoded = scrydantic::encode(boxed);
  REQUIRE_FALSE(encoded);
  CHECK(encoded.error().message ==
        "$.renamed is a std::indirect left valueless by a move");
  CHECK(encoded.error().model_message.empty());
  CHECK(taken->zed == "z");
}

// ---- Recursive models --------------------------------------------------------

// Named rather than anonymous, so the schema definition names below read as a
// program's own would.
namespace recursive {

// A tree: std::vector allows an incomplete element type.
struct Category {
  std::vector<Category> children;
  std::string name;
};

// A linked list, which ends where its optional is disengaged.
struct Chain {
  [[= scrydantic::skip_null]] std::optional<std::indirect<Chain>> next;
  int value{};
};

struct Employee;

struct Team {
  std::vector<Employee> members;
  std::string name;
};

struct Employee {
  std::optional<std::indirect<Team>> leads;
  std::string name;
};

// A variant alternative of the shape Chain has, which makes the variant's own
// conversions ask what Chain's do (decode_failed()).
struct[[= scrydantic::tag{"link"}]] Link {
  std::optional<std::indirect<Link>> next;
};

struct[[= scrydantic::tag{"end"}]] End {};

using Linked = std::variant<End, Link>;

// A recursive type below a root that is not.
struct Catalog {
  [[= scrydantic::description{"The top category"}]] Category root;
  std::string title;
};

// An expression tree. The leaf comes first, so a default-constructed Expression
// holds no further Expression.
struct Negate;
struct Sum;

struct[[= scrydantic::tag{"literal"}]] Literal {
  double value{};
};

using Expression = std::variant<Literal, Negate, Sum>;

struct[[= scrydantic::tag{"negate"}]] Negate {
  std::indirect<Expression> operand;
};

struct[[= scrydantic::tag{"sum"}]] Sum {
  std::vector<Expression> terms;
};

template <typename Value> struct Node {
  std::vector<Node> children;
  Value value{};
};

struct Forest {
  std::vector<Node<int>> numbers;
  std::optional<Node<std::string>> words;
};

// One identifier in two namespaces.
namespace other {
struct Category {
  std::vector<Category> children;
};
} // namespace other

struct Shelves {
  Category mine;
  other::Category theirs;
};

// A tagged class that recurses both as a variant alternative and as a plain
// value, so its schema needs both of its forms as definitions.
struct Folder;

struct[[= scrydantic::tag{"file"}]] File {
  std::string name;
};

using Entry = std::variant<File, Folder>;

struct[[= scrydantic::tag{"folder"}]] Folder {
  std::vector<Entry> entries;
  std::vector<Folder> folders;
};

struct Drive {
  Folder top;
};

// Cycles that hold no std::optional, std::vector, or later alternative.
struct Endless {
  std::indirect<Endless> next;
};

struct Pong;

struct Ping {
  std::indirect<Pong> pong;
};

struct Pong {
  Ping ping;
};

struct Halves {
  std::array<std::indirect<Halves>, 2> halves;
};

struct Wrapped;

using WrappedFirst = std::variant<Wrapped, Literal>;

struct[[= scrydantic::tag{"wrapped"}]] Wrapped {
  std::indirect<WrappedFirst> inner;
};

// A reference always binds to something, so a chain of them closes on itself.
struct Ring {
  const Ring& next;
};

// Cycles that end: an empty std::array holds nothing, and a vector may be empty.
struct NoHalves {
  std::array<std::indirect<NoHalves>, 0> none;
};

struct Rows {
  std::array<std::vector<Rows>, 2> rows;
};

// Spin<1> and Spin<-1> contain each other, and the sign tells their names apart.
template <int Sign> struct Spin {
  std::vector<Spin<-Sign>> flipped;
};

struct Spins {
  Spin<1> up;
};

// Every kind of template argument that a definition name spells.
enum class Axis : std::uint8_t {
  rows,
  columns,
};

template <typename Element, int Offset, Axis Along, bool Dense, char Mark> struct Grid {
  std::vector<Grid> cells;
};

template <typename... Tags> struct Marked {
  std::vector<Marked> marks;
};

template <typename Value> struct Outer {
  struct Inner {
    std::vector<Inner> more;
  };
};

template <std::int64_t Low, std::uint64_t High> struct Range {
  std::vector<Range> parts;
};

struct Spelled {
  Grid<std::array<std::optional<std::uint8_t>, 2>, -2, Axis::columns, true, 'x'> grid;
  Marked<std::variant<Literal, Sum>, std::indirect<double>, std::vector<char>,
         std::string, long>
      marked;
  Outer<float>::Inner inner;
  Range<std::numeric_limits<std::int64_t>::min(),
        std::numeric_limits<std::uint64_t>::max()>
      range;
};

// long and long long have one width, and so one name.
template <typename Integer> struct Tally {
  std::vector<Tally> parts;
  Integer count{};
};

struct Tallies {
  Tally<long> wide;
  Tally<long long> wider;
};

// Recursive types whose names cannot be spelled.
template <typename Tag> struct Tagged {
  std::vector<Tagged> children;
};

struct Pointed {
  Tagged<int*> tagged;
};

template <double Scale> struct Scaled {
  std::vector<Scaled> parts;
};

struct Scales {
  Scaled<0.5> half;
};

struct Größe {
  std::vector<Größe> parts;
};

struct Sizes {
  Größe size;
};

} // namespace recursive

namespace {

// An anonymous namespace is part of the qualified name a definition derives.
struct Outline {
  std::vector<Outline> sections;
};

struct Report {
  Outline outline;
};

// A class local to a function has no stable name.
consteval std::string_view local_definition_diagnostic() {
  struct Local {
    std::vector<Local> more;
  };
  struct Holder {
    Local local;
  };
  return scrydantic::detail::supported_diagnostic<Holder>();
}

// A chain of `length` links, valued from `length - 1` at its head down to 0.
[[nodiscard]] recursive::Chain chain_of(const std::size_t length) {
  recursive::Chain head{.next = std::nullopt, .value = 0};
  for (int value = 1; std::cmp_less(value, length); ++value) {
    recursive::Chain link{.next = std::indirect<recursive::Chain>(std::move(head)),
                          .value = value};
    head = std::move(link);
  }
  return head;
}

// The canonical text of chain_of(length), with `last` as its last link.
[[nodiscard]] std::string chain_text(const std::size_t length,
                                     const std::string_view last = R"({"value":0})") {
  std::string text{};
  for (std::size_t link = 1; link < length; ++link) {
    text += R"({"next":)";
  }
  text += last;
  for (std::size_t value = 1; value < length; ++value) {
    text += R"(,"value":)" + std::to_string(value) + "}";
  }
  return text;
}

} // namespace

static_assert(scrydantic::SupportedValue<recursive::Category>);
static_assert(scrydantic::ToolArguments<recursive::Category>);
static_assert(scrydantic::SupportedValue<recursive::Chain>);
static_assert(scrydantic::SupportedValue<recursive::Team>);
static_assert(scrydantic::SupportedValue<recursive::Employee>);
static_assert(scrydantic::SupportedValue<recursive::Catalog>);
static_assert(scrydantic::SupportedValue<recursive::Expression>);
static_assert(scrydantic::SupportedValue<recursive::Forest>);
static_assert(scrydantic::SupportedValue<recursive::Shelves>);
static_assert(scrydantic::SupportedValue<recursive::Drive>);
static_assert(scrydantic::SupportedValue<recursive::NoHalves>);
static_assert(scrydantic::SupportedValue<recursive::Rows>);
static_assert(scrydantic::SupportedValue<std::indirect<recursive::Chain>>);

static_assert(!scrydantic::SupportedValue<recursive::Endless>);
static_assert(!scrydantic::Encodable<recursive::Endless>);
static_assert(!scrydantic::SupportedValue<recursive::Ping>);
static_assert(!scrydantic::SupportedValue<recursive::Halves>);
// Encoding builds nothing, so a variant to encode may hold its later alternative.
static_assert(!scrydantic::Decodable<recursive::WrappedFirst>);
static_assert(scrydantic::Encodable<recursive::WrappedFirst>);
static_assert(!scrydantic::Encodable<recursive::Ring>);
static_assert(scrydantic::SupportedValue<recursive::Spins>);
static_assert(scrydantic::SupportedValue<recursive::Spelled>);
// Only a schema needs names: the rest of the codec takes these types.
static_assert(sizeof(long) == sizeof(long long));
static_assert(!scrydantic::SupportedValue<recursive::Tallies>);
static_assert(scrydantic::Decodable<recursive::Tallies>);
static_assert(!scrydantic::SupportedValue<recursive::Pointed>);
static_assert(scrydantic::Decodable<recursive::Pointed>);
static_assert(!scrydantic::SupportedValue<recursive::Scales>);
static_assert(!scrydantic::SupportedValue<recursive::Sizes>);
// A recursive root needs no name of its own.
static_assert(scrydantic::SupportedValue<recursive::Tagged<int*>>);

static_assert(
    mentions(scrydantic::detail::supported_diagnostic<recursive::Endless>(),
             "recursive::Endless::next: recursive::Endless contains itself through no "
             "std::optional, std::vector, or variant alternative after the first, so "
             "default-constructing it, as decoding does, never ends"));
static_assert(mentions(
    scrydantic::detail::encodable_diagnostic<recursive::Endless>(),
    "recursive::Endless::next: recursive::Endless contains itself through no "
    "std::optional, std::vector, or variant, so every value of it is infinite or "
    "cyclic and cannot be JSON"));
static_assert(mentions(scrydantic::detail::supported_diagnostic<recursive::Ping>(),
                       "recursive::Ping::pong.ping: recursive::Ping contains itself"));
static_assert(
    mentions(scrydantic::detail::supported_diagnostic<recursive::Halves>(),
             "recursive::Halves::halves[]: recursive::Halves contains itself"));
static_assert(
    mentions(scrydantic::detail::decodable_diagnostic<recursive::WrappedFirst>(),
             "recursive::Wrapped::inner<recursive::Wrapped>: recursive::Wrapped "
             "contains itself through no std::optional, std::vector, or variant "
             "alternative after the first"));
static_assert(mentions(scrydantic::detail::encodable_diagnostic<recursive::Ring>(),
                       "recursive::Ring::next: recursive::Ring contains itself through "
                       "no std::optional, std::vector, or variant"));
static_assert(mentions(scrydantic::detail::supported_diagnostic<recursive::Tallies>(),
                       "recursive::Tallies has the recursive types recursive::Tally<"));
static_assert(mentions(scrydantic::detail::supported_diagnostic<recursive::Tallies>(),
                       ", which both map to the schema definition name "
                       "\"recursive.Tally-int64\""));
static_assert(mentions(scrydantic::detail::supported_diagnostic<recursive::Pointed>(),
                       "recursive::Pointed has the recursive type "
                       "recursive::Tagged<int*>, which no schema definition name can "
                       "spell: int* is not a type that a definition name spells"));
static_assert(mentions(scrydantic::detail::supported_diagnostic<recursive::Scales>(),
                       ", which no schema definition name can spell: "));
static_assert(mentions(scrydantic::detail::supported_diagnostic<recursive::Scales>(),
                       " is not a constant that a definition name spells; an argument "
                       "constant must be an integer, a bool, or an enumerator"));
static_assert(
    mentions(scrydantic::detail::supported_diagnostic<recursive::Sizes>(),
             "recursive::Sizes has the recursive type recursive::Größe, which "
             "no schema definition name can spell: recursive::Größe has a "
             "name that is not ASCII letters, digits, and '_'"));
static_assert(mentions(local_definition_diagnostic(),
                       "Local is local to a function; declare it in a namespace"));

// A recursive root refers to itself as "#", and keeps "type":"object" at the top.
static_assert(
    scrydantic::schema_v<recursive::Category> ==
    R"({"additionalProperties":false,"properties":{"children":{"items":{"$ref":"#"},"type":"array"},"name":{"type":"string"}},"required":["children","name"],"type":"object"})");
static_assert(scrydantic::input_schema_v<recursive::Category> ==
              scrydantic::schema_v<recursive::Category>);
static_assert(scrydantic::schema_v<std::indirect<recursive::Category>> ==
              scrydantic::schema_v<recursive::Category>);

static_assert(
    scrydantic::schema_v<recursive::Chain> ==
    R"({"additionalProperties":false,"properties":{"next":{"anyOf":[{"$ref":"#"},{"type":"null"}]},"value":{"maximum":2147483647,"minimum":-2147483648,"type":"integer"}},"required":[],"type":"object"})");

// Below a root that is not recursive, the same type is a definition.
static_assert(
    scrydantic::schema_v<std::vector<recursive::Chain>> ==
    R"({"$defs":{"recursive.Chain":{"additionalProperties":false,"properties":{"next":{"anyOf":[{"$ref":"#/$defs/recursive.Chain"},{"type":"null"}]},"value":{"maximum":2147483647,"minimum":-2147483648,"type":"integer"}},"required":[],"type":"object"}},"items":{"$ref":"#/$defs/recursive.Chain"},"type":"array"})");

static_assert(
    scrydantic::schema_v<recursive::Team> ==
    R"({"$defs":{"recursive.Employee":{"additionalProperties":false,"properties":{"leads":{"anyOf":[{"$ref":"#"},{"type":"null"}]},"name":{"type":"string"}},"required":["leads","name"],"type":"object"}},"additionalProperties":false,"properties":{"members":{"items":{"$ref":"#/$defs/recursive.Employee"},"type":"array"},"name":{"type":"string"}},"required":["members","name"],"type":"object"})");

// A description stays beside the reference it describes.
static_assert(
    scrydantic::schema_v<recursive::Catalog> ==
    R"({"$defs":{"recursive.Category":{"additionalProperties":false,"properties":{"children":{"items":{"$ref":"#/$defs/recursive.Category"},"type":"array"},"name":{"type":"string"}},"required":["children","name"],"type":"object"}},"additionalProperties":false,"properties":{"root":{"$ref":"#/$defs/recursive.Category","description":"The top category"},"title":{"type":"string"}},"required":["root","title"],"type":"object"})");

// Only the alternatives that recurse are definitions; the leaf stays in place.
static_assert(
    scrydantic::schema_v<recursive::Expression> ==
    R"({"$defs":{"recursive.Negate":{"additionalProperties":false,"properties":{"operand":{"anyOf":[{"additionalProperties":false,"properties":{"type":{"enum":["literal"],"type":"string"},"value":{"type":"number"}},"required":["type"],"type":"object"},{"$ref":"#/$defs/recursive.Negate"},{"$ref":"#/$defs/recursive.Sum"}]},"type":{"enum":["negate"],"type":"string"}},"required":["operand","type"],"type":"object"},"recursive.Sum":{"additionalProperties":false,"properties":{"terms":{"items":{"anyOf":[{"additionalProperties":false,"properties":{"type":{"enum":["literal"],"type":"string"},"value":{"type":"number"}},"required":["type"],"type":"object"},{"$ref":"#/$defs/recursive.Negate"},{"$ref":"#/$defs/recursive.Sum"}]},"type":"array"},"type":{"enum":["sum"],"type":"string"}},"required":["terms","type"],"type":"object"}},"anyOf":[{"additionalProperties":false,"properties":{"type":{"enum":["literal"],"type":"string"},"value":{"type":"number"}},"required":["type"],"type":"object"},{"$ref":"#/$defs/recursive.Negate"},{"$ref":"#/$defs/recursive.Sum"}]})");

static_assert(
    scrydantic::schema_v<recursive::Forest> ==
    R"({"$defs":{"recursive.Node-int32":{"additionalProperties":false,"properties":{"children":{"items":{"$ref":"#/$defs/recursive.Node-int32"},"type":"array"},"value":{"maximum":2147483647,"minimum":-2147483648,"type":"integer"}},"required":["children"],"type":"object"},"recursive.Node-string":{"additionalProperties":false,"properties":{"children":{"items":{"$ref":"#/$defs/recursive.Node-string"},"type":"array"},"value":{"type":"string"}},"required":["children"],"type":"object"}},"additionalProperties":false,"properties":{"numbers":{"items":{"$ref":"#/$defs/recursive.Node-int32"},"type":"array"},"words":{"anyOf":[{"$ref":"#/$defs/recursive.Node-string"},{"type":"null"}]}},"required":["numbers","words"],"type":"object"})");

static_assert(
    scrydantic::schema_v<recursive::Shelves> ==
    R"({"$defs":{"recursive.Category":{"additionalProperties":false,"properties":{"children":{"items":{"$ref":"#/$defs/recursive.Category"},"type":"array"},"name":{"type":"string"}},"required":["children","name"],"type":"object"},"recursive.other.Category":{"additionalProperties":false,"properties":{"children":{"items":{"$ref":"#/$defs/recursive.other.Category"},"type":"array"}},"required":["children"],"type":"object"}},"additionalProperties":false,"properties":{"mine":{"$ref":"#/$defs/recursive.Category"},"theirs":{"$ref":"#/$defs/recursive.other.Category"}},"required":["mine","theirs"],"type":"object"})");

static_assert(
    scrydantic::schema_v<recursive::Drive> ==
    R"({"$defs":{"recursive.Folder":{"additionalProperties":false,"properties":{"entries":{"items":{"anyOf":[{"additionalProperties":false,"properties":{"name":{"type":"string"},"type":{"enum":["file"],"type":"string"}},"required":["name","type"],"type":"object"},{"$ref":"#/$defs/recursive.Folder"}]},"type":"array"},"folders":{"items":{"$ref":"#/$defs/recursive.Folder-untagged"},"type":"array"},"type":{"enum":["folder"],"type":"string"}},"required":["entries","folders","type"],"type":"object"},"recursive.Folder-untagged":{"additionalProperties":false,"properties":{"entries":{"items":{"anyOf":[{"additionalProperties":false,"properties":{"name":{"type":"string"},"type":{"enum":["file"],"type":"string"}},"required":["name","type"],"type":"object"},{"$ref":"#/$defs/recursive.Folder"}]},"type":"array"},"folders":{"items":{"$ref":"#/$defs/recursive.Folder-untagged"},"type":"array"}},"required":["entries","folders"],"type":"object"}},"additionalProperties":false,"properties":{"top":{"$ref":"#/$defs/recursive.Folder-untagged"}},"required":["top"],"type":"object"})");

static_assert(
    scrydantic::schema_v<Report> ==
    R"({"$defs":{"anonymous.Outline":{"additionalProperties":false,"properties":{"sections":{"items":{"$ref":"#/$defs/anonymous.Outline"},"type":"array"}},"required":["sections"],"type":"object"}},"additionalProperties":false,"properties":{"outline":{"$ref":"#/$defs/anonymous.Outline"}},"required":["outline"],"type":"object"})");

// The sign marker keeps Spin<1> and Spin<-1> apart.
static_assert(
    scrydantic::schema_v<recursive::Spins> ==
    R"({"$defs":{"recursive.Spin-1":{"additionalProperties":false,"properties":{"flipped":{"items":{"$ref":"#/$defs/recursive.Spin-minus1"},"type":"array"}},"required":["flipped"],"type":"object"},"recursive.Spin-minus1":{"additionalProperties":false,"properties":{"flipped":{"items":{"$ref":"#/$defs/recursive.Spin-1"},"type":"array"}},"required":["flipped"],"type":"object"}},"additionalProperties":false,"properties":{"up":{"$ref":"#/$defs/recursive.Spin-1"}},"required":["up"],"type":"object"})");

static_assert(mentions(scrydantic::schema_v<recursive::Spelled>,
                       R"("$ref":"#/$defs/recursive.Grid-array-optional-uint8-2-)"
                       R"(minus2-columns-true-120")"));
static_assert(mentions(scrydantic::schema_v<recursive::Spelled>,
                       R"("$ref":"#/$defs/recursive.Marked-variant-recursive.Literal-)"
                       R"(recursive.Sum-indirect-double-vector-char-string-int64")"));
static_assert(mentions(scrydantic::schema_v<recursive::Spelled>,
                       R"("$ref":"#/$defs/recursive.Outer-float.Inner")"));
static_assert(mentions(scrydantic::schema_v<recursive::Spelled>,
                       R"("$ref":"#/$defs/recursive.Range-minus9223372036854775808-)"
                       R"(18446744073709551615")"));

TEST_CASE("a recursive tree round trips and reports failures deep inside it") {
  const auto text = std::string{
      R"({"children":[{"children":[{"children":[],"name":"c"}],"name":"b"}],"name":"a"})"};
  const auto decoded = decode_text<recursive::Category>(text);
  REQUIRE(decoded);
  REQUIRE(decoded->children.size() == 1);
  CHECK(decoded->children[0].children[0].name == "c");
  CHECK(scrydantic::encode(*decoded)->text == text);

  auto failed = decode_text<recursive::Category>(
      R"({"children":[{"children":[{"children":[],"name":7}],"name":"b"}],"name":"a"})");
  REQUIRE_FALSE(failed);
  CHECK(failed.error().model_message ==
        "$.children[0].children[0].name must be a string");
  failed =
      decode_text<recursive::Category>(R"({"children":[{"name":"b"}],"name":"a"})");
  REQUIRE_FALSE(failed);
  CHECK(failed.error().model_message == "$.children[0].children is a required member");
}

TEST_CASE("issues deep in a recursive tree are each reported at their path") {
  const auto failed = decode_text<recursive::Category>(
      R"({"children":[{"children":[],"name":1},{"children":[],"name":2}],"name":3})");
  REQUIRE_FALSE(failed);
  CHECK(failed.error().issues.size() == 3);
  CHECK(failed.error().model_message == "$.children[0].name must be a string\n"
                                        "$.children[1].name must be a string\n"
                                        "$.name must be a string");
}

TEST_CASE("results of recursive types fail without asking how a failure converts") {
  const auto linked =
      decode_text<recursive::Linked>(R"({"next":{"next":null},"type":"link"})");
  REQUIRE(linked);
  CHECK(std::get<recursive::Link>(*linked).next.has_value());
  const auto bad_link =
      decode_text<recursive::Linked>(R"({"next":{"next":7},"type":"link"})");
  REQUIRE_FALSE(bad_link);
  CHECK(bad_link.error().model_message == "$.next.next must be an object");

  const auto bad_optional =
      decode_text<std::optional<recursive::Chain>>(R"({"value":"one"})");
  REQUIRE_FALSE(bad_optional);
  CHECK(bad_optional.error().model_message == "$.value must be an integer");
  const auto bad_array = decode_text<std::array<recursive::Chain, 1>>(R"([[]])");
  REQUIRE_FALSE(bad_array);
  CHECK(bad_array.error().model_message == "$[0] must be an object");
  const auto bad_vector = decode_text<std::vector<recursive::Chain>>("{}");
  REQUIRE_FALSE(bad_vector);
  CHECK(bad_vector.error().model_message == "$ must be an array");
}

TEST_CASE("a linked list ends at a disengaged optional, which skip_null omits") {
  const recursive::Chain chain{
      .next = std::indirect<recursive::Chain>(
          recursive::Chain{.next = std::nullopt, .value = 2}),
      .value = 1,
  };
  const auto text = std::string{R"({"next":{"value":2},"value":1})"};
  CHECK(scrydantic::encode(chain)->text == text);

  const auto decoded = decode_text<recursive::Chain>(text);
  REQUIRE(decoded);
  REQUIRE(decoded->next.has_value());
  CHECK((*decoded->next)->value == 2);
  CHECK_FALSE((*decoded->next)->next.has_value());
  CHECK(decode_text<recursive::Chain>(R"({"next":null,"value":1})"));

  const auto failed =
      decode_text<recursive::Chain>(R"({"next":{"next":{"value":"x"}},"value":1})");
  REQUIRE_FALSE(failed);
  CHECK(failed.error().model_message == "$.next.next.value must be an integer");
}

TEST_CASE("mutually recursive aggregates round trip") {
  const auto text =
      std::string{R"({"members":[{"leads":{"members":[{"leads":null,)"
                  R"("name":"b"}],"name":"inner"},"name":"a"}],"name":"outer"})"};
  const auto decoded = decode_text<recursive::Team>(text);
  REQUIRE(decoded);
  REQUIRE(decoded->members[0].leads.has_value());
  CHECK((*decoded->members[0].leads)->members[0].name == "b");
  CHECK(scrydantic::encode(*decoded)->text == text);

  const auto failed = decode_text<recursive::Team>(
      R"({"members":[{"leads":{"members":[{"leads":null,"name":1}],"name":"t"},)"
      R"("name":"e"}],"name":"x"})");
  REQUIRE_FALSE(failed);
  CHECK(failed.error().model_message ==
        "$.members[0].leads.members[0].name must be a string");
}

TEST_CASE("a recursive tagged variant decodes by tag at every depth") {
  const auto text = std::string{
      R"({"operand":{"terms":[{"type":"literal","value":1},{"operand":)"
      R"({"type":"literal","value":2},"type":"negate"}],"type":"sum"},"type":"negate"})"};
  const auto decoded = decode_text<recursive::Expression>(text);
  REQUIRE(decoded);
  const auto& sum =
      std::get<recursive::Sum>(*std::get<recursive::Negate>(*decoded).operand);
  REQUIRE(sum.terms.size() == 2);
  CHECK(std::get<recursive::Literal>(sum.terms[0]).value == 1.0);
  CHECK(std::get<recursive::Literal>(*std::get<recursive::Negate>(sum.terms[1]).operand)
            .value == 2.0);
  CHECK(scrydantic::encode(*decoded)->text == text);

  auto failed = decode_text<recursive::Expression>(
      R"({"operand":{"terms":[{"type":"literal"},{"type":"product"}],"type":"sum"},)"
      R"("type":"negate"})");
  REQUIRE_FALSE(failed);
  CHECK(failed.error().model_message ==
        "$.operand.terms[1].type must be one of: literal, negate, sum");
  failed = decode_text<recursive::Expression>(R"({"type":"negate"})");
  REQUIRE_FALSE(failed);
  CHECK(failed.error().model_message == "$.operand is a required member");
}

TEST_CASE("each specialization of a recursive template recurses on its own") {
  const auto text =
      std::string{R"({"numbers":[{"children":[{"children":[],"value":2}],)"
                  R"("value":1}],"words":{"children":[],"value":"w"}})"};
  const auto decoded = decode_text<recursive::Forest>(text);
  REQUIRE(decoded);
  CHECK(decoded->numbers[0].children[0].value == 2);
  CHECK(decoded->words->value == "w");
  CHECK(scrydantic::encode(*decoded)->text == text);

  const auto failed = decode_text<recursive::Forest>(
      R"({"numbers":[{"children":[{"children":[],"value":"2"}],"value":1}],"words":null})");
  REQUIRE_FALSE(failed);
  CHECK(failed.error().model_message ==
        "$.numbers[0].children[0].value must be an integer");
}

TEST_CASE("recursion nests as deep as the parser reads and no deeper") {
  // 256 links nest 256 objects, the parser's limit.
  const auto deepest = chain_text(256);
  const auto decoded = decode_text<recursive::Chain>(deepest);
  REQUIRE(decoded);
  CHECK(scrydantic::encode(*decoded)->text == deepest);
  CHECK(scrydantic::encode(chain_of(256))->text == deepest);

  std::string path = "$";
  for (std::size_t link = 1; link < 256; ++link) {
    path += ".next";
  }
  const auto failed =
      decode_text<recursive::Chain>(chain_text(256, R"({"value":"0"})"));
  REQUIRE_FALSE(failed);
  CHECK(failed.error().model_message == path + ".value must be an integer");

  // One link more is past the parser's limit, and the writer stops there too.
  const auto too_deep = decode_text<recursive::Chain>(chain_text(257));
  REQUIRE_FALSE(too_deep);
  CHECK(too_deep.error().message == "JSON text is not valid JSON");
  const auto encoded = scrydantic::encode(chain_of(257));
  REQUIRE_FALSE(encoded);
  CHECK(encoded.error().message ==
        path + ".next is an array or object nested more than 256 deep, so the value "
               "cannot be JSON");
}
