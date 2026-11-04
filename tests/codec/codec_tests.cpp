#include <array>
#include <bit>
#include <catch2/catch_test_macros.hpp>
#include <charconv>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <random>
#include <scrydantic/annotations.hpp>
#include <scrydantic/codec.hpp>
#include <scrydantic/diagnostics.hpp>
#include <scrydantic/json.hpp>
#include <string>
#include <string_view>
#include <system_error>
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

static_assert(mentions(scrydantic::supported_value_diagnostic<Outer>(),
                       "does not satisfy scrydantic::SupportedValue: "));
static_assert(mentions(scrydantic::supported_value_diagnostic<Outer>(),
                       "Outer::nested.letters[]: wchar_t is a character type"));
static_assert(mentions(scrydantic::supported_value_diagnostic<HoldsBad>(),
                       "HoldsBad::items[]<"));
static_assert(mentions(scrydantic::supported_value_diagnostic<HoldsBad>(),
                       "BadAlternative>.letter: char is a character type"));
static_assert(
    mentions(scrydantic::supported_value_diagnostic<std::variant<Unannotated>>(),
             "without a scrydantic::tag annotation"));
static_assert(
    mentions(scrydantic::supported_value_diagnostic<std::variant<Left, int>>(),
             "has the alternative int, which is not a plain aggregate"));
static_assert(
    mentions(scrydantic::supported_value_diagnostic<std::variant<Left, AlsoLeft>>(),
             "that share the tag \"left\""));
static_assert(
    mentions(scrydantic::supported_value_diagnostic<std::variant<TypedAlternative>>(),
             "TypedAlternative::kind: has the JSON key \"type\""));
static_assert(mentions(scrydantic::supported_value_diagnostic<Colliding>(),
                       "has members first and second that both map to the JSON key "
                       "\"key\""));
static_assert(mentions(scrydantic::supported_value_diagnostic<MisplacedSkip>(),
                       "MisplacedSkip::value: is not a std::optional"));
static_assert(mentions(scrydantic::supported_value_diagnostic<HoldsView>(),
                       "HoldsView::text: "));
static_assert(mentions(scrydantic::supported_value_diagnostic<HoldsView>(),
                       " is encode-only borrowed text; decoding and schemas take "
                       "std::string"));
static_assert(mentions(scrydantic::decodable_diagnostic<ConstView>(),
                       "ConstView::value: is a const member"));
static_assert(mentions(scrydantic::supported_value_diagnostic<Document>(),
                       "scrydantic::Json is raw JSON, which has no schema"));
static_assert(mentions(scrydantic::tool_arguments_diagnostic<Shape>(),
                       "is not a plain aggregate; tool arguments decode from one "
                       "JSON object"));
static_assert(mentions(scrydantic::supported_value_diagnostic<Bytes>(),
                       "Bytes::data[]: unsigned char is a character type, as "
                       "std::int8_t and std::uint8_t are; use std::int16_t or "
                       "std::uint16_t for a number"));
static_assert(mentions(scrydantic::supported_value_diagnostic<Flags>(),
                       "Flags::flags: std::vector<bool> is std::vector<bool>, whose "
                       "elements are not addressable; use std::vector<std::uint16_t>, "
                       "or std::array<bool, N> for a fixed size"));
// Each replacement the diagnostics suggest is itself supported.
static_assert(scrydantic::SupportedValue<Words>);
static_assert(scrydantic::SupportedValue<FlagsFixed>);
static_assert(scrydantic::supported_value_diagnostic<Drawing>().empty());
// Each family's diagnostic agrees with its concept, and an entry point's
// diagnostic is the one the concept's diagnostic gives.
static_assert(scrydantic::encodable_diagnostic<BorrowedRequest>().empty());
static_assert(mentions(scrydantic::encodable_diagnostic<Bytes>(),
                       "does not satisfy scrydantic::Encodable: "));
static_assert(scrydantic::decodable_diagnostic<Payload>().empty());
static_assert(mentions(scrydantic::decodable_diagnostic<const Payload>(),
                       "is cv-qualified or a reference"));
static_assert(scrydantic::tool_arguments_diagnostic<Drawing>().empty());
static_assert(scrydantic::encodable_diagnostic<Bytes>() ==
              scrydantic::detail::encodable_diagnostic<Bytes>());

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

  const auto direct = scrydantic::encode_verbatim(drawing);
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

  // With several bad members, the first in declaration order is reported, even
  // though its key sorts later.
  rejected = decode_text<Renamed>(R"({"a_first":1,"max_tokens":"x"})");
  REQUIRE_FALSE(rejected);
  CHECK(rejected.error().model_message == "$.max_tokens must be an integer");
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

  const auto direct = scrydantic::encode_verbatim(body);
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

TEST_CASE("encode_verbatim() keeps payload text that encode() respells") {
  // Wider than any 64-bit integer, so canonical form reads it as a double.
  const Payload wide{.body = {.text = R"({"z": 12345678901234567890123, "a": 1.50})"}};
  const auto verbatim = scrydantic::encode_verbatim(wide);
  REQUIRE(verbatim);
  CHECK(verbatim->text == R"({"body":{"z": 12345678901234567890123, "a": 1.50}})");
  const auto canonical = scrydantic::encode(wide);
  REQUIRE(canonical);
  CHECK(canonical->text == R"({"body":{"a":1.5,"z":1.2345678901234568E22}})");

  // Numbers take std::to_chars' shortest text; keys and escapes stay canonical.
  CHECK(scrydantic::encode_verbatim(1e-7)->text == "1e-07");
  CHECK(scrydantic::encode(1e-7)->text == "1E-7");
  CHECK(scrydantic::encode_verbatim(Renamed{.limit = 3, .zed = "a\nb"})->text ==
        R"({"a_first":"a\nb","max_tokens":3})");
}

TEST_CASE("encode_verbatim() fails where encode() fails") {
  const auto non_finite =
      scrydantic::encode_verbatim(std::vector<double>{1.0, std::nan("")});
  REQUIRE_FALSE(non_finite);
  CHECK(non_finite.error().message == "$[1] must be finite");
  CHECK(non_finite.error().model_message.empty());

  const auto invalid = scrydantic::encode_verbatim(Payload{.body = {.text = "{"}});
  REQUIRE_FALSE(invalid);
  CHECK(invalid.error().message == "$.body is not valid JSON");

  const auto bad_text = scrydantic::encode_verbatim(DocText{.text = "bad \xff byte"});
  REQUIRE_FALSE(bad_text);
  CHECK(bad_text.error().message ==
        "the value holds a string that is not valid UTF-8, so it cannot be JSON");
}

// ---- Canonical spelling ------------------------------------------------------

namespace {

// encode() writes canonical text directly. It must write exactly the bytes that
// canonicalizing the verbatim writer's text writes, so the two spellings agree.
template <typename Type>
[[nodiscard]] std::optional<std::string> spelling_mismatch(const Type& value) {
  const auto direct = scrydantic::encode(value);
  const auto verbatim = scrydantic::encode_verbatim(value);
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
    if (!scrydantic::is_valid_json(payload)) {
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
  CHECK(scrydantic::encode_verbatim(text)->text == expected);
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

  const auto verbatim = scrydantic::encode_verbatim(deep);
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
  CHECK(scrydantic::encode_verbatim<std::optional<int>&>(value)->text == "42");
}

TEST_CASE("an invalid spliced JSON payload fails encoding at its path") {
  for (const auto* text : {"", "   ", "{", R"({"a":1} 2)", "nul"}) {
    const BorrowedRequest body{
        .model = "m",
        .tools = std::vector<BorrowedTool>{{.input_schema = {.text = text}}},
    };
    const auto encoded = scrydantic::encode_verbatim(body);
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

  const auto direct = scrydantic::encode_verbatim(*decoded);
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
