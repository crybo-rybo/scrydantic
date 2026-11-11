// Validators: when field and class checks run, in what order, how a rejection
// becomes an issue, what of it the model and the host are each told, and every
// check annotation the compiler rejects, with its diagnostic.

#include <catch2/catch_test_macros.hpp>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <memory>
#include <meta>
#include <optional>
#include <scrydantic/annotations.hpp>
#include <scrydantic/codec.hpp>
#include <scrydantic/diagnostics.hpp>
#include <scrydantic/error.hpp>
#include <scrydantic/json.hpp>
#include <stdexcept>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace {

using scrydantic::check;
using scrydantic::Status;

// How many times each counting validator has run.
struct Calls {
  std::size_t nonempty{};
  std::size_t capitalized{};
  std::size_t even{};
  std::size_t ordered{};
  std::size_t sorted{};
};

Calls calls{};

[[nodiscard]] Status reject(std::string model_message, std::string host_message = {}) {
  return std::unexpected(
      scrydantic::rejection(std::move(model_message), std::move(host_message)));
}

Status is_even(const std::int32_t& value) noexcept {
  ++calls.even;
  return value % 2 == 0 ? Status{} : reject("must be even");
}

Status is_known_city(const std::string& city) {
  return city == "Oslo" || city == "Lima"
             ? Status{}
             : reject("must be a known city",
                      "names " + city + ", which the gazetteer lacks");
}

// is_known_city with the rejection built under the name a tool handler uses, which
// is one function and so makes no difference to what decoding reports.
Status is_listed_city(const std::string& city) {
  return city == "Oslo" || city == "Lima"
             ? Status{}
             : std::unexpected(scrydantic::tool_error(
                   "must be a known city",
                   "names " + city + ", which the gazetteer lacks"));
}

// A rejection with text for the host alone.
Status is_not_banned(const std::string& name) {
  return name != "mallory" ? Status{}
                           : std::unexpected(scrydantic::Error{
                                 .message = "is on the internal ban list"});
}

// A rejection with no text at all.
Status is_not_reserved(const std::string& name) {
  return name != "root" ? Status{} : std::unexpected(scrydantic::Error{});
}

// A rejection with text for the model alone, which the host is told too.
Status is_not_shouted(const std::string& text) {
  return text.find('!') == std::string::npos
             ? Status{}
             : std::unexpected(scrydantic::Error{.model_message = "must not shout"});
}

Status is_nonempty(const std::string& text) {
  ++calls.nonempty;
  return text.empty() ? reject("must not be empty") : Status{};
}

// Relies on is_nonempty, which runs first: front() of an empty string is
// undefined, so this one must never see one.
Status is_capitalized(const std::string& text) {
  ++calls.capitalized;
  return text.front() >= 'A' && text.front() <= 'Z' ? Status{}
                                                    : reject("must be capitalized");
}

Status fails_loudly(const std::int32_t&) {
  throw std::runtime_error{"validator threw"};
}

struct Interval;
Status is_ordered(const Interval& interval);

struct[[= check<&is_ordered>]] Interval {
  std::int32_t low{};
  std::int32_t high{};
};

Status is_ordered(const Interval& interval) {
  ++calls.ordered;
  return interval.low <= interval.high ? Status{}
                                       : reject("must have low at most high");
}

struct Trip {
  [[= check<&is_known_city>]] std::string from;
  [[ = scrydantic::length{.max = 4}, = check<&is_known_city> ]] std::string to;
  [[= check<&is_even>]] std::optional<std::int32_t> passengers;
};

struct Route {
  [[= check<&is_known_city>]] std::string from;
  [[= check<&is_listed_city>]] std::string to;
};

// A renamed member's validator is reported under its wire name.
struct Seating {
  [[ = scrydantic::name{"seat_count"}, = check<&is_even> ]] std::int32_t seats{};
};

struct Account {
  [[ = check<&is_not_banned>, = check<&is_not_reserved> ]] std::string name;
  [[= check<&is_not_shouted>]] std::string motto;
};

struct Title {
  [[
    = scrydantic::length{.max = 16}, = check<&is_nonempty>, = check<&is_capitalized>
  ]] std::string text;
};

struct Schedule {
  Interval window;
  std::vector<Interval> breaks;
  [[= check<&is_even>]] std::int32_t slots = 3;
};

struct[[= scrydantic::tag{"span"}]] Span {
  Interval interval;
};

struct[[= scrydantic::tag{"point"}]] Point {
  [[= check<&is_even>]] std::int32_t at{};
};

using Moment = std::variant<Span, Point>;

struct Evens {
  std::vector<Point> points;
};

struct Loud {
  [[= check<&fails_loudly>]] std::int32_t value{};
};

// A field validator takes the value an indirect owns.
struct Boxed {
  [[= check<&is_even>]] std::optional<std::indirect<std::int32_t>> count;
};

// A recursive model whose class validator inspects the children of its level.
struct Tree;
Status is_sorted(const Tree& tree);

struct[[= check<&is_sorted>]] Tree {
  std::vector<Tree> children;
  std::int32_t value{};
};

Status is_sorted(const Tree& tree) {
  ++calls.sorted;
  for (std::size_t index = 1; index < tree.children.size(); ++index) {
    if (tree.children[index - 1].value > tree.children[index].value) {
      return reject("must have its children in ascending order");
    }
  }
  return {};
}

template <typename Type>
[[nodiscard]] scrydantic::Result<Type> decode_text(const std::string_view text) {
  return scrydantic::decode<Type>(scrydantic::Json{.text = std::string{text}});
}

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

// Each issue as `kind path reason`, so a failed comparison shows all three.
[[nodiscard]] std::vector<std::string> issue_lines(const scrydantic::Error& error) {
  std::vector<std::string> lines{};
  for (const auto& issue : error.issues) {
    lines.push_back(enumerator_name(issue.kind) + " " +
                    scrydantic::json_path(issue.path) + " " + issue.reason);
  }
  return lines;
}

// Copies the error rather than moving it out. GCC cannot see that REQUIRE_FALSE
// ends the test when `decoded` holds a value, and at -O2 it follows that path,
// where the moved-out Error's short strings point into the storage that the
// value's destructor then frees, and reports -Wfree-nonheap-object. Any
// std::expected whose value owns memory draws it so.
template <typename Type>
[[nodiscard]] scrydantic::Error rejection(const std::string_view text) {
  const auto decoded = decode_text<Type>(text);
  REQUIRE_FALSE(decoded);
  return decoded.error();
}

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

// A validator adds nothing to the schema: it is no part of the contract.
static_assert(
    scrydantic::schema_v<Trip> ==
    R"({"additionalProperties":false,"properties":{"from":{"type":"string"},)"
    R"("passengers":{"anyOf":[{"maximum":2147483647,"minimum":-2147483648,)"
    R"("type":"integer"},{"type":"null"}]},"to":{"maxLength":4,"type":"string"}},)"
    R"("required":["from","passengers","to"],"type":"object"})");

TEST_CASE("a field validator's rejection is an issue in the model's words") {
  CHECK(decode_text<Trip>(R"({"from":"Oslo","passengers":2,"to":"Lima"})"));

  const auto error = rejection<Trip>(R"({"from":"Rome","passengers":3,"to":"Lima"})");
  CHECK(issue_lines(error) == std::vector<std::string>{
                                  "failed_check $.from must be a known city",
                                  "failed_check $.passengers must be even",
                              });
  CHECK(error.issues.front() ==
        scrydantic::Issue{.kind = scrydantic::IssueKind::failed_check,
                          .path = {"from"},
                          .reason = "must be a known city"});
  // The host is told the validator's own text, and the model only its words.
  CHECK(error.model_message == "$.from must be a known city\n"
                               "$.passengers must be even");
  CHECK(error.message == "$.from names Rome, which the gazetteer lacks\n"
                         "$.passengers must be even");
}

TEST_CASE("a validator that returns tool_error is reported as one that returns "
          "rejection") {
  const auto error = rejection<Route>(R"({"from":"Rome","to":"Rome"})");
  CHECK(issue_lines(error) == std::vector<std::string>{
                                  "failed_check $.from must be a known city",
                                  "failed_check $.to must be a known city",
                              });
  CHECK(error.model_message == "$.from must be a known city\n"
                               "$.to must be a known city");
  CHECK(error.message == "$.from names Rome, which the gazetteer lacks\n"
                         "$.to names Rome, which the gazetteer lacks");
}

TEST_CASE("a validator that gives the model no text tells it only that the value is "
          "not valid") {
  const auto banned = rejection<Account>(R"({"motto":"","name":"mallory"})");
  CHECK(issue_lines(banned) ==
        std::vector<std::string>{"failed_check $.name is not valid"});
  CHECK(banned.model_message == "$.name is not valid");
  CHECK(banned.message == "$.name is on the internal ban list");

  // With no text for either, both are told the same.
  const auto reserved = rejection<Account>(R"({"motto":"","name":"root"})");
  CHECK(reserved.model_message == "$.name is not valid");
  CHECK(reserved.message == "$.name is not valid");

  // Text for the model alone is the host's too.
  const auto shouted = rejection<Account>(R"({"motto":"go!","name":"eve"})");
  CHECK(shouted.model_message == "$.motto must not shout");
  CHECK(shouted.message == "$.motto must not shout");
}

TEST_CASE("a member's validators run after its constraints, in order, until one "
          "rejects") {
  calls = {};
  CHECK(decode_text<Title>(R"({"text":"Hello"})"));
  CHECK(calls.nonempty == 1);
  CHECK(calls.capitalized == 1);

  // A missed constraint keeps every validator from running.
  calls = {};
  CHECK(issue_lines(rejection<Title>(R"({"text":"an overlong lowercase title"})")) ==
        std::vector<std::string>{
            "violated_constraint $.text must have at most 16 characters"});
  CHECK(calls.nonempty == 0);
  CHECK(calls.capitalized == 0);

  // A rejection keeps the later validators from running, so is_capitalized never
  // sees the empty string it could not handle.
  calls = {};
  CHECK(issue_lines(rejection<Title>(R"({"text":""})")) ==
        std::vector<std::string>{"failed_check $.text must not be empty"});
  CHECK(calls.nonempty == 1);
  CHECK(calls.capitalized == 0);

  calls = {};
  CHECK(issue_lines(rejection<Title>(R"({"text":"hello"})")) ==
        std::vector<std::string>{"failed_check $.text must be capitalized"});
  CHECK(calls.capitalized == 1);
}

TEST_CASE("a field validator sees only a decoded value from the input") {
  calls = {};
  // A disengaged optional has no value to check.
  CHECK(decode_text<Trip>(R"({"from":"Oslo","passengers":null,"to":"Lima"})"));
  CHECK(calls.even == 0);

  // A value that fails to decode is never checked.
  CHECK(issue_lines(
            rejection<Trip>(R"({"from":"Oslo","passengers":"two","to":"Lima"})")) ==
        std::vector<std::string>{"wrong_type $.passengers must be an integer"});
  CHECK(calls.even == 0);

  // An absent member keeps its default, the program's own value.
  const auto schedule = decode_text<Schedule>(R"({"breaks":[],"window":{}})");
  REQUIRE(schedule);
  CHECK(schedule->slots == 3);
  CHECK(calls.even == 0);

  // Encoding checks nothing, verbatim or canonical.
  CHECK(scrydantic::encode(Trip{.from = "Atlantis", .to = "Mu", .passengers = 5}));
  CHECK(scrydantic::encode_verbatim(
      Trip{.from = "Atlantis", .to = "Mu", .passengers = 5}));
  CHECK(calls.even == 0);
}

TEST_CASE("a class validator runs once every member of an object is valid") {
  calls = {};
  CHECK(decode_text<Interval>(R"({"high":2,"low":1})"));
  CHECK(calls.ordered == 1);

  calls = {};
  CHECK(issue_lines(rejection<Interval>(R"({"high":1,"low":2})")) ==
        std::vector<std::string>{"failed_check $ must have low at most high"});
  CHECK(calls.ordered == 1);

  // A member's issue, or an unknown member, leaves the object unchecked.
  calls = {};
  CHECK(issue_lines(rejection<Interval>(R"({"high":"x","low":2})")) ==
        std::vector<std::string>{"wrong_type $.high must be an integer"});
  CHECK(
      issue_lines(rejection<Interval>(R"({"high":1,"low":2,"width":1})")) ==
      std::vector<std::string>{R"(unknown_member $ contains unknown member "width")"});
  CHECK(calls.ordered == 0);
}

TEST_CASE("class validators report at the object's path, wherever it is") {
  const auto schedule = rejection<Schedule>(
      R"({"breaks":[{"high":5,"low":1},{"high":0,"low":9}],"slots":7,)"
      R"("window":{"high":0,"low":1}})");
  CHECK(issue_lines(schedule) ==
        std::vector<std::string>{
            "failed_check $.window must have low at most high",
            "failed_check $.breaks[1] must have low at most high",
            "failed_check $.slots must be even",
        });

  CHECK(issue_lines(
            rejection<Moment>(R"({"interval":{"high":0,"low":1},"type":"span"})")) ==
        std::vector<std::string>{"failed_check $.interval must have low at most high"});
  CHECK(issue_lines(rejection<Moment>(R"({"at":1,"type":"point"})")) ==
        std::vector<std::string>{"failed_check $.at must be even"});
}

TEST_CASE("validator issues sit among the others, and count toward max_issues") {
  const auto mixed = rejection<Trip>(R"({"from":7,"passengers":1,"to":"Kyoto"})");
  CHECK(issue_lines(mixed) ==
        std::vector<std::string>{
            "wrong_type $.from must be a string",
            "violated_constraint $.to must have at most 4 characters",
            "failed_check $.passengers must be even",
        });

  // Each element past the limit is never decoded, so the validator runs on the
  // 65th, whose issue stops the decode, and on no element after it.
  std::string text{R"({"points":[)"};
  for (std::size_t index = 0; index < 100; ++index) {
    text.append(index == 0 ? "" : ",").append(R"({"at":1})");
  }
  text.append("]}");
  calls = {};
  const auto many = rejection<Evens>(text);
  CHECK(many.issues.size() == scrydantic::max_issues);
  CHECK(calls.even == scrydantic::max_issues + 1);
  CHECK(many.message.ends_with("$.points[63].at must be even\n"
                               "further issues past the first 64 were not reported"));
  CHECK(many.model_message == many.message);
}

// The wire names of validated types are the codec's own.
static_assert(scrydantic::keys_v<Seating>.size() == 1 &&
              scrydantic::keys_v<Seating>[0] == "seat_count");
static_assert(scrydantic::tags_v<Moment>.size() == 2 &&
              scrydantic::tags_v<Moment>[0] == "span" &&
              scrydantic::tags_v<Moment>[1] == "point");

TEST_CASE("a renamed member's validator issue is at its wire name") {
  calls = {};
  CHECK(decode_text<Seating>(R"({"seat_count":4})"));
  CHECK(rejection<Seating>(R"({"seat_count":3})").issues ==
        std::vector<scrydantic::Issue>{{.kind = scrydantic::IssueKind::failed_check,
                                        .path = {"seat_count"},
                                        .reason = "must be even"}});
  CHECK(calls.even == 2);
}

TEST_CASE("a field validator sees through an indirect") {
  calls = {};
  CHECK(decode_text<Boxed>(R"({"count":null})"));
  CHECK(decode_text<Boxed>(R"({"count":4})"));
  CHECK(issue_lines(rejection<Boxed>(R"({"count":3})")) ==
        std::vector<std::string>{"failed_check $.count must be even"});
  CHECK(calls.even == 2);
}

TEST_CASE("a recursive model's class validator runs at every level, innermost first") {
  calls = {};
  CHECK(decode_text<Tree>(R"({"children":[{"children":[],"value":1},)"
                          R"({"children":[{"children":[],"value":0}],"value":2}],)"
                          R"("value":0})"));
  CHECK(calls.sorted == 4);

  // The inner level fails its validator, so the root, whose children are out of
  // order too, is not checked: one of its members has an issue.
  calls = {};
  CHECK(issue_lines(rejection<Tree>(
            R"({"children":[{"children":[{"children":[],"value":2},)"
            R"({"children":[],"value":1}],"value":1},{"children":[],"value":0}],)"
            R"("value":0})")) ==
        std::vector<std::string>{
            "failed_check $.children[0] must have its children in ascending order"});
  CHECK(calls.sorted == 4);

  // A member's issue deep inside leaves every level around it unchecked.
  calls = {};
  CHECK(
      issue_lines(rejection<Tree>(R"({"children":[{"children":[{"children":[],)"
                                  R"("value":"x"}],"value":0}],"value":0})")) ==
      std::vector<std::string>{"wrong_type $.children[0].children[0].value must be an "
                               "integer"});
  CHECK(calls.sorted == 0);
}

TEST_CASE("an exception from a validator leaves the decode") {
  CHECK_THROWS_AS(decode_text<Loud>(R"({"value":1})"), std::runtime_error);
  CHECK(decode_text<Loud>("{}"));
}

// ---- Rejections --------------------------------------------------------------

namespace {

Status by_value(std::int32_t) { return {}; }
bool returns_bool(const std::int32_t&) { return true; }
Status of_optional(const std::optional<std::int32_t>&) { return {}; }
Status of_text(const std::string&) { return {}; }

struct ByValue {
  [[= check<&by_value>]] std::int32_t value{};
};

struct ReturnsBool {
  [[= check<&returns_bool>]] std::int32_t value{};
};

struct OfOptional {
  [[= check<&of_optional>]] std::optional<std::int32_t> value;
};

struct WrongType {
  [[= check<&of_text>]] std::int32_t value{};
};

struct NotAFunction {
  [[= check<42>]] std::int32_t value{};
};

struct NullValidator {
  [[= check<static_cast<Status (*)(const std::int32_t&)>(nullptr)>]] std::int32_t
      value{};
};

struct Twice {
  [[ = check<&is_even>, = check<&is_even> ]] std::int32_t value{};
};

struct[[= check<&is_even>]] ClassOfInt {
  std::int32_t value{};
};

struct HoldsTwice {
  std::optional<Twice> nested;
};

} // namespace

static_assert(
    mentions(scrydantic::supported_value_diagnostic<ByValue>(),
             "ByValue::value: carries scrydantic::check<&{anonymous}::by_value>, "
             "whose validator is std::expected<void, scrydantic::Error>(int); "
             "a validator here takes const int& and returns scrydantic::Status"));
static_assert(mentions(scrydantic::supported_value_diagnostic<ReturnsBool>(),
                       "ReturnsBool::value: carries "
                       "scrydantic::check<&{anonymous}::returns_bool>, whose validator "
                       "is bool(const int&)"));
// An optional member's validator takes its value, as its constraints bound it.
static_assert(mentions(scrydantic::supported_value_diagnostic<OfOptional>(),
                       "; a validator here takes const int& and returns "
                       "scrydantic::Status"));
static_assert(
    mentions(scrydantic::supported_value_diagnostic<WrongType>(),
             "WrongType::value: carries scrydantic::check<&{anonymous}::of_text>"));
static_assert(mentions(scrydantic::supported_value_diagnostic<NotAFunction>(),
                       "NotAFunction::value: carries scrydantic::check<42>, whose "
                       "argument is not a pointer to a function"));
static_assert(mentions(scrydantic::supported_value_diagnostic<NullValidator>(),
                       "NullValidator::value: carries a scrydantic::check of a null "
                       "function pointer"));
static_assert(mentions(scrydantic::supported_value_diagnostic<Twice>(),
                       "Twice::value: carries scrydantic::check<&{anonymous}::is_even> "
                       "twice"));
static_assert(
    mentions(scrydantic::supported_value_diagnostic<ClassOfInt>(),
             "ClassOfInt carries scrydantic::check<&{anonymous}::is_even>, whose "
             "validator is std::expected<void, scrydantic::Error>(const int&) "
             "noexcept; a validator here takes const {anonymous}::ClassOfInt& "
             "and returns scrydantic::Status"));
static_assert(mentions(scrydantic::supported_value_diagnostic<HoldsTwice>(),
                       "HoldsTwice::nested.value: carries scrydantic::check<"));
// The validators these tests use are accepted, noexcept or not.
static_assert(scrydantic::supported_value_diagnostic<Trip>().empty());
static_assert(scrydantic::supported_value_diagnostic<Schedule>().empty());
// Each family's public diagnostic gives the validator's problem as its concept
// rejects it, and none for the validated types these tests decode.
static_assert(!scrydantic::Encodable<ByValue>);
static_assert(
    mentions(scrydantic::encodable_diagnostic<ByValue>(),
             "does not satisfy scrydantic::Encodable: {anonymous}::ByValue::value: "
             "carries scrydantic::check<&{anonymous}::by_value>"));
static_assert(!scrydantic::Decodable<ByValue>);
static_assert(
    mentions(scrydantic::decodable_diagnostic<ByValue>(),
             "does not satisfy scrydantic::Decodable: {anonymous}::ByValue::value: "
             "carries scrydantic::check<&{anonymous}::by_value>"));
static_assert(!scrydantic::ToolArguments<ByValue>);
static_assert(
    mentions(scrydantic::tool_arguments_diagnostic<ByValue>(),
             "does not satisfy scrydantic::ToolArguments: {anonymous}::ByValue::value: "
             "carries scrydantic::check<&{anonymous}::by_value>"));
static_assert(scrydantic::Encodable<Trip> &&
              scrydantic::encodable_diagnostic<Trip>().empty());
static_assert(scrydantic::Decodable<Trip> &&
              scrydantic::decodable_diagnostic<Trip>().empty());
static_assert(scrydantic::ToolArguments<Trip> &&
              scrydantic::tool_arguments_diagnostic<Trip>().empty());
