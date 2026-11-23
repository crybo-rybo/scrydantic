// scrydantic::name on an enumerator: the string it gives the enumerator in the
// schema, in encoding, in decoding, and in the reason an unknown string is
// rejected with, and every annotation on an enumerator that the compiler rejects,
// with its diagnostic.

#include <catch2/catch_test_macros.hpp>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <scrydantic/annotations.hpp>
#include <scrydantic/codec.hpp>
#include <scrydantic/diagnostics.hpp>
#include <scrydantic/error.hpp>
#include <string>
#include <string_view>
#include <vector>

namespace {

using scrydantic::IssueKind;
using scrydantic::name;

// A value with punctuation, beside an enumerator that keeps its identifier.
enum class Mode : std::uint8_t {
  tools_call[[= name{"tools/call"}]],
  other,
};

struct HasMode {
  Mode mode;

  bool operator==(const HasMode&) const = default;
};

// Values that are C++ keywords.
enum class Access : std::uint8_t {
  public_[[= name{"public"}]],
  private_[[= name{"private"}]],
  internal,
};

// A one-enumerator enum is a constant: the schema states the only value, and
// decoding accepts nothing else.
enum class Version : std::uint8_t {
  v2[[= name{"2.0"}]],
};

struct Envelope {
  Version version;
  std::int32_t id{};

  bool operator==(const Envelope&) const = default;
};

struct Request {
  std::optional<Mode> preferred;
  std::vector<Mode> allowed;

  bool operator==(const Request&) const = default;
};

// A name is any text, so it is escaped where JSON needs it.
enum class Quoted : std::uint8_t {
  said[[= name{"say \"hi\""}]],
  tabbed[[= name{"a\tb"}]],
};

// An annotation that is not Scrydantic's is the program's own, and is left alone.
struct Note {};

constexpr Note note{};

enum class Annotated : std::uint8_t {
  first[[= note]][[= name{"1st"}]],
  second[[= 2]],
};

static_assert(
    scrydantic::schema_v<HasMode> ==
    R"({"additionalProperties":false,"properties":{"mode":{"enum":["tools/call","other"],"type":"string"}},"required":["mode"],"type":"object"})");
static_assert(scrydantic::schema_v<Mode> ==
              R"({"enum":["tools/call","other"],"type":"string"})");
static_assert(scrydantic::schema_v<Access> ==
              R"({"enum":["public","private","internal"],"type":"string"})");
static_assert(
    scrydantic::schema_v<Envelope> ==
    R"({"additionalProperties":false,"properties":{"id":{"default":0,"maximum":2147483647,"minimum":-2147483648,"type":"integer"},"version":{"enum":["2.0"],"type":"string"}},"required":["version"],"type":"object"})");
static_assert(
    scrydantic::schema_v<Request> ==
    R"({"additionalProperties":false,"properties":{"allowed":{"items":{"enum":["tools/call","other"],"type":"string"},"type":"array"},"preferred":{"anyOf":[{"enum":["tools/call","other"],"type":"string"},{"type":"null"}]}},"required":["allowed","preferred"],"type":"object"})");
static_assert(scrydantic::schema_v<Quoted> ==
              R"({"enum":["say \"hi\"","a\tb"],"type":"string"})");
static_assert(scrydantic::schema_v<Annotated> ==
              R"({"enum":["1st","second"],"type":"string"})");

TEST_CASE("a named enumerator is written as its name") {
  CHECK(scrydantic::encode(HasMode{.mode = Mode::tools_call})->text ==
        R"({"mode":"tools/call"})");
  CHECK(scrydantic::encode(HasMode{.mode = Mode::other})->text ==
        R"({"mode":"other"})");
  CHECK(scrydantic::encode(Mode::tools_call)->text == R"("tools/call")");
  CHECK(scrydantic::encode(Access::public_)->text == R"("public")");
  CHECK(scrydantic::encode(Access::private_)->text == R"("private")");
  CHECK(scrydantic::encode(Access::internal)->text == R"("internal")");
  CHECK(scrydantic::encode(Quoted::said)->text == R"("say \"hi\"")");
  CHECK(scrydantic::encode(Quoted::tabbed)->text == R"("a\tb")");
  CHECK(scrydantic::encode(Annotated::first)->text == R"("1st")");
  CHECK(scrydantic::encode(Annotated::second)->text == R"("second")");
}

TEST_CASE("a named enumerator is read by its name and not by its identifier") {
  CHECK(scrydantic::decode<HasMode>(R"({"mode":"tools/call"})").value() ==
        HasMode{.mode = Mode::tools_call});
  CHECK(scrydantic::decode<HasMode>(R"({"mode":"other"})").value() ==
        HasMode{.mode = Mode::other});
  CHECK(scrydantic::decode<Access>(R"("public")").value() == Access::public_);
  CHECK(scrydantic::decode<Access>(R"("private")").value() == Access::private_);
  CHECK(scrydantic::decode<Quoted>(R"("say \"hi\"")").value() == Quoted::said);
  CHECK(scrydantic::decode<Quoted>(R"("a\tb")").value() == Quoted::tabbed);

  const auto identifier = scrydantic::decode<HasMode>(R"({"mode":"tools_call"})");
  REQUIRE_FALSE(identifier);
  REQUIRE(identifier.error().issues.size() == 1);
  CHECK(identifier.error().issues[0].kind == IssueKind::unknown_enumerator);
  CHECK(identifier.error().model_message ==
        "$.mode is not a declared enumerator; must be one of: tools/call, other");

  const auto keyword = scrydantic::decode<Access>(R"("public_")");
  REQUIRE_FALSE(keyword);
  CHECK(keyword.error().model_message ==
        "$ is not a declared enumerator; must be one of: public, private, internal");
}

TEST_CASE("a one-enumerator enum is a constant member") {
  CHECK(scrydantic::encode(Envelope{.version = Version::v2, .id = 7})->text ==
        R"({"id":7,"version":"2.0"})");
  CHECK(scrydantic::decode<Envelope>(R"({"id":7,"version":"2.0"})").value() ==
        Envelope{.version = Version::v2, .id = 7});

  const auto other = scrydantic::decode<Envelope>(R"({"id":7,"version":"1.0"})");
  REQUIRE_FALSE(other);
  CHECK(other.error().model_message ==
        "$.version is not a declared enumerator; must be one of: 2.0");
  // The constant is a string, as the schema says, so the number is not it.
  const auto number = scrydantic::decode<Envelope>(R"({"id":7,"version":2.0})");
  REQUIRE_FALSE(number);
  CHECK(number.error().model_message == "$.version must be an enumerator name");
  const auto absent = scrydantic::decode<Envelope>(R"({"id":7})");
  REQUIRE_FALSE(absent);
  CHECK(absent.error().model_message == "$.version is a required member");
}

TEST_CASE("named enumerators round-trip inside an optional and a vector") {
  const Request request{.preferred = Mode::tools_call,
                        .allowed = {Mode::other, Mode::tools_call}};
  const auto encoded = scrydantic::encode(request);
  REQUIRE(encoded);
  CHECK(encoded->text ==
        R"({"allowed":["other","tools/call"],"preferred":"tools/call"})");
  CHECK(scrydantic::decode<Request>(*encoded).value() == request);

  const Request empty{.preferred = std::nullopt, .allowed = {}};
  CHECK(scrydantic::encode(empty)->text == R"({"allowed":[],"preferred":null})");
  CHECK(scrydantic::decode<Request>(*scrydantic::encode(empty)).value() == empty);

  const auto rejected = scrydantic::decode<Request>(
      R"({"allowed":["other","tools_call"],"preferred":"tools_call"})");
  REQUIRE_FALSE(rejected);
  CHECK(rejected.error().model_message ==
        "$.preferred is not a declared enumerator; must be one of: tools/call, other\n"
        "$.allowed[1] is not a declared enumerator; must be one of: tools/call, other");
}

// ---- Rejected at compile time ------------------------------------------------

// A name that lands on another enumerator's name, written or kept.
enum class Twice : std::uint8_t {
  first[[= name{"same"}]],
  second[[= name{"same"}]],
};

enum class Shadowed : std::uint8_t {
  on[[= name{"off"}]],
  off,
};

enum class TwoNames : std::uint8_t {
  value[[= scrydantic::name{"a"}]][[= scrydantic::name{"b"}]],
};

scrydantic::Status accept(const std::int32_t& /*value*/) { return {}; }

enum class Described : std::uint8_t {
  plain,
  value[[= scrydantic::description{"The usual one"}]],
};

enum class Bounded : std::uint8_t {
  value[[= name{"v"}]][[= scrydantic::range{.min = 0}]],
};

enum class Tagged : std::uint8_t {
  value[[= scrydantic::tag{"v"}]],
};

enum class Skipped : std::uint8_t {
  value[[= scrydantic::skip_null]],
};

enum class Checked : std::uint8_t {
  value[[= scrydantic::check<&accept>]],
};

struct HoldsDescribed {
  std::vector<Described> values;
};

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

// One check serves the three value families, so each rejects such an enum.
static_assert(!scrydantic::SupportedValue<Twice>);
static_assert(!scrydantic::Encodable<Twice>);
static_assert(!scrydantic::Decodable<Twice>);
static_assert(mentions(scrydantic::supported_value_diagnostic<Twice>(),
                       "Twice has enumerators first and second that both map to the "
                       "JSON string \"same\""));
static_assert(mentions(scrydantic::decodable_diagnostic<Shadowed>(),
                       "Shadowed has enumerators on and off that both map to the JSON "
                       "string \"off\""));
static_assert(
    mentions(scrydantic::encodable_diagnostic<TwoNames>(),
             "TwoNames::value has more than one scrydantic::name annotation"));
static_assert(mentions(scrydantic::supported_value_diagnostic<Described>(),
                       "Described::value carries scrydantic::description, which does "
                       "not apply to enumerators; an enumerator takes only "
                       "scrydantic::name"));
// Beside a name, too: the name does not excuse the annotation next to it.
static_assert(mentions(scrydantic::decodable_diagnostic<Bounded>(),
                       "Bounded::value carries scrydantic::range, which does not "
                       "apply to enumerators"));
static_assert(mentions(scrydantic::encodable_diagnostic<Tagged>(),
                       "Tagged::value carries scrydantic::tag, which does not apply "
                       "to enumerators"));
// An annotation is called by the name of its type.
static_assert(mentions(scrydantic::encodable_diagnostic<Skipped>(),
                       "Skipped::value carries scrydantic::skip_null_t, which does "
                       "not apply to enumerators"));
static_assert(mentions(scrydantic::decodable_diagnostic<Checked>(),
                       "Checked::value carries scrydantic::check_t, which does not "
                       "apply to enumerators"));
// Where the enum is a member's type, the path to the member comes first.
static_assert(mentions(scrydantic::supported_value_diagnostic<HoldsDescribed>(),
                       "HoldsDescribed::values[]: {anonymous}::Described::value "
                       "carries scrydantic::description"));
// An annotation that is not Scrydantic's is no problem.
static_assert(scrydantic::supported_value_diagnostic<Annotated>().empty());

} // namespace
