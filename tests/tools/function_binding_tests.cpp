// Function binding: arguments_t names the argument object of any function,
// apply() calls the function with one and returns exactly what it returns, and
// annotated_functions_of() finds the functions that carry a library's own
// annotation. None of them needs a scrydantic::tool annotation.

#include <array>
#include <catch2/catch_test_macros.hpp>
#include <chrono>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <meta>
#include <scrydantic/scrydantic.hpp>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

namespace {

// A host's call context. Scrydantic only passes it through.
struct Session {
  std::string user{};
};

// An argument object that a function takes whole.
struct Span {
  std::int32_t first{};
  std::int32_t last{};
};

// A result that Scrydantic cannot encode, and that cannot be copied.
struct Lease {
  std::unique_ptr<std::int32_t> slot{};
};

} // namespace

// Functions with no Scrydantic annotation, in each of the three argument forms,
// without and with a leading context.
namespace ledger {

inline std::int32_t count() { return 3; }

inline std::int32_t width(Span span) { return span.last - span.first; }

inline std::string label(std::string name, std::int32_t copies) {
  return name + "x" + std::to_string(copies);
}

inline std::string owner(const Session& session) { return session.user; }

inline std::string claim(const Session& session, const Span& span) {
  return session.user + ":" + std::to_string(span.last - span.first);
}

inline std::string sign(const Session& session, std::string note, bool loud) {
  return session.user + (loud ? "!" : ".") + note;
}

// What each returns is its caller's to deal with.
inline scrydantic::Result<std::int32_t> halve(std::int32_t value) {
  if (value % 2 != 0) {
    return std::unexpected(scrydantic::tool_error("must be even"));
  }
  return value / 2;
}

inline Lease lease(std::int32_t slot) {
  return Lease{.slot = std::make_unique<std::int32_t>(slot)};
}

// A member of the argument object is moved into its parameter, whichever way
// the parameter takes it.
inline std::string join(std::string&& head, const std::string& tail) {
  const std::string taken = std::move(head);
  return taken + tail;
}

// The tool annotation changes nothing about how the parameters are read.
[[= scrydantic::tool{"Repeat a word"}]] inline std::string repeat(std::string word,
                                                                  std::int32_t times) {
  std::string repeated{};
  for (std::int32_t index = 0; index < times; ++index) {
    repeated += word;
  }
  return repeated;
}

} // namespace ledger

namespace {

class Till {
public:
  static std::int32_t limit() { return 100; }
  static std::int32_t width(Span span) { return span.last - span.first; }
  static std::string label(std::string name, std::int32_t copies) {
    return name + "x" + std::to_string(copies);
  }
  static std::string owner(const Session& session) { return session.user; }
  static std::string claim(const Session& session, Span span) {
    return session.user + ":" + std::to_string(span.last - span.first);
  }
  static std::string sign(const Session& session, std::string note, bool loud) {
    return session.user + (loud ? "!" : ".") + note;
  }

  std::int32_t total() const { return total_; }
  std::int32_t add(Span span) { return total_ += span.last - span.first; }
  std::string stamp(std::string note, std::int32_t copies) const {
    return title_ + ":" + note + "x" + std::to_string(copies + total_);
  }
  std::string teller(const Session& session) const {
    return session.user + "@" + title_;
  }
  std::int32_t credit(const Session& session, Span span) {
    return total_ +=
           static_cast<std::int32_t>(session.user.size()) * (span.last - span.first);
  }
  std::string memo(const Session& session, std::string note, bool loud) const {
    return session.user + "@" + title_ + (loud ? "!" : ".") + note;
  }

  void reset() { total_ = 0; }
  const std::string& title() const { return title_; }

  // Explicit object parameters: by const reference, by reference, and by value,
  // which copies the object the function is called on.
  std::int32_t scaled(this const Till& self, std::int32_t factor) {
    return self.total_ * factor;
  }
  std::int32_t raised(this Till& self, std::int32_t by) { return self.total_ += by; }
  std::int32_t trial(this Till self, std::int32_t by) { return self.total_ += by; }
  std::string seen(this const Till& self, const Session& session, std::int32_t times) {
    return session.user + "@" + self.title_ + "x" + std::to_string(times);
  }

private:
  std::int32_t total_{};
  std::string title_{"till"};
};

// A member function of a base is called on an object of the class derived from it.
class NightTill : public Till {};

// The argument object of `Function`, decoded from `text` as a library decodes
// it: from a parsed JsonView, into the type that the binding it was handed
// names. `Binding` is forwarded, as the header of tools.hpp asks of a wrapper.
template <std::meta::info Function, typename Context = void,
          typename Binding = typename[:scrydantic::binding_of(Function, ^^Context):]>
[[nodiscard]] scrydantic::arguments_t<Function, Context, Binding>
decoded(const std::string_view text) {
  using Arguments = scrydantic::arguments_t<Function, Context, Binding>;
  const auto view = scrydantic::JsonView::parse(text);
  REQUIRE(view);
  auto arguments = scrydantic::decode<Arguments>(*view);
  REQUIRE(arguments);
  return std::move(*arguments);
}

constexpr std::string_view no_members =
    R"({"additionalProperties":false,"properties":{},"required":[],"type":"object"})";

constexpr std::string_view name_and_copies =
    R"({"additionalProperties":false,"properties":{"copies":{"maximum":2147483647,)"
    R"("minimum":-2147483648,"type":"integer"},"name":{"type":"string"}},)"
    R"("required":["copies","name"],"type":"object"})";

constexpr std::string_view note_and_loud =
    R"({"additionalProperties":false,"properties":{"loud":{"type":"boolean"},)"
    R"("note":{"type":"string"}},"required":["loud","note"],"type":"object"})";

} // namespace

// ---- The argument object, at compile time ---------------------------------------

// Nothing, the lone aggregate itself, or an aggregate synthesized from the list.
static_assert(std::is_empty_v<scrydantic::arguments_t<^^ledger::count>>);
static_assert(std::same_as<scrydantic::arguments_t<^^ledger::width>, Span>);
static_assert(scrydantic::keys_v<scrydantic::arguments_t<^^ledger::label>> ==
              std::array<std::string_view, 2>{"copies", "name"});
static_assert(scrydantic::input_schema_v<scrydantic::arguments_t<^^ledger::count>> ==
              no_members);
static_assert(scrydantic::input_schema_v<scrydantic::arguments_t<^^ledger::width>> ==
              scrydantic::input_schema_v<Span>);
static_assert(scrydantic::input_schema_v<scrydantic::arguments_t<^^ledger::label>> ==
              name_and_copies);

// A leading `const Context&` is not part of it, and a cv-qualified or reference
// parameter is its plain type.
static_assert(std::is_empty_v<scrydantic::arguments_t<^^ledger::owner, Session>>);
static_assert(std::same_as<scrydantic::arguments_t<^^ledger::claim, Session>, Span>);
static_assert(
    scrydantic::input_schema_v<scrydantic::arguments_t<^^ledger::sign, Session>> ==
    note_and_loud);
// Without that context the same parameter is an argument: alone, the object.
static_assert(std::same_as<scrydantic::arguments_t<^^ledger::owner>, Session>);
// A context that the function does not take leaves its arguments as they were.
static_assert(std::same_as<scrydantic::arguments_t<^^ledger::width, Session>, Span>);

// Member functions form theirs the same way, and an explicit object parameter
// is not part of it either.
static_assert(std::is_empty_v<scrydantic::arguments_t<^^Till::limit>>);
static_assert(std::is_empty_v<scrydantic::arguments_t<^^Till::total>>);
static_assert(std::same_as<scrydantic::arguments_t<^^Till::add>, Span>);
static_assert(scrydantic::input_schema_v<scrydantic::arguments_t<^^Till::label>> ==
              name_and_copies);
static_assert(scrydantic::keys_v<scrydantic::arguments_t<^^Till::stamp>> ==
              std::array<std::string_view, 2>{"copies", "note"});
static_assert(scrydantic::keys_v<scrydantic::arguments_t<^^Till::scaled>> ==
              std::array<std::string_view, 1>{"factor"});
static_assert(scrydantic::keys_v<scrydantic::arguments_t<^^Till::seen, Session>> ==
              std::array<std::string_view, 1>{"times"});

// One name, one type: a second mention at the same point names the same object,
// and a tool's argument object is the one bind() gives it.
static_assert(std::same_as<scrydantic::arguments_t<^^ledger::label>,
                           scrydantic::arguments_t<^^ledger::label>>);
static_assert(scrydantic::ToolArguments<scrydantic::arguments_t<^^ledger::label>>);
static_assert(scrydantic::describe<^^ledger::repeat>()[0].input_schema ==
              scrydantic::input_schema_v<scrydantic::arguments_t<^^ledger::repeat>>);

// ---- What apply() returns, at compile time ---------------------------------------

namespace {

template <typename Arguments> [[nodiscard]] Arguments&& given();

} // namespace

static_assert(
    std::same_as<decltype(scrydantic::apply<^^ledger::count>({})), std::int32_t>);
static_assert(std::same_as<decltype(scrydantic::apply<^^ledger::halve>(
                               given<scrydantic::arguments_t<^^ledger::halve>>())),
                           scrydantic::Result<std::int32_t>>);
static_assert(std::same_as<decltype(scrydantic::apply<^^ledger::lease>(
                               given<scrydantic::arguments_t<^^ledger::lease>>())),
                           Lease>);
static_assert(
    std::is_void_v<decltype(scrydantic::apply<^^Till::reset>(given<Till&>(), {}))>);
static_assert(
    std::same_as<decltype(scrydantic::apply<^^Till::title>(given<const Till&>(), {})),
                 const std::string&>);

// ---- Diagnostics -------------------------------------------------------------

namespace rejected_functions {

inline std::int32_t deref(const std::int32_t* pointer) { return *pointer; }
inline std::int32_t unnamed(std::int32_t, std::int32_t) { return 0; }
inline std::int32_t bounded([[= scrydantic::range{.max = 9}]] std::int32_t level) {
  return level;
}
inline std::string late(std::string note, const Session&) { return note; }
inline void fill(std::int32_t& total) { total = 0; }
void gone(std::int32_t value) = delete;
template <typename Value> Value same(Value value) { return value; }
inline constexpr std::int32_t limit = 3;

} // namespace rejected_functions

// Empty for every function above, whatever it returns and whatever it carries.
static_assert(scrydantic::arguments_diagnostic<^^ledger::label>().empty());
static_assert(scrydantic::arguments_diagnostic<^^ledger::lease>().empty());
static_assert(scrydantic::arguments_diagnostic<^^ledger::repeat>().empty());
static_assert(scrydantic::arguments_diagnostic<^^ledger::sign, Session>().empty());
static_assert(scrydantic::arguments_diagnostic<^^Till::add>().empty());
static_assert(scrydantic::arguments_diagnostic<^^Till::seen, Session>().empty());

// The rules are bind()'s, and the text names the call the user wrote.
static_assert(scrydantic::arguments_diagnostic<^^rejected_functions::deref>() ==
              "scrydantic::apply<^^rejected_functions::deref>(): "
              "rejected_functions::deref has parameter `pointer`: const int* is not a "
              "supported reflected value");
static_assert(
    scrydantic::arguments_diagnostic<^^rejected_functions::deref>("routes::add") ==
    "routes::add<^^rejected_functions::deref>(): rejected_functions::deref has "
    "parameter `pointer`: const int* is not a supported reflected value");
static_assert(scrydantic::arguments_diagnostic<^^rejected_functions::unnamed>() ==
              "scrydantic::apply<^^rejected_functions::unnamed>(): "
              "rejected_functions::unnamed has the unnamed parameter 1; an argument "
              "synthesized from a parameter is named after it");
static_assert(scrydantic::arguments_diagnostic<^^rejected_functions::bounded>() ==
              "scrydantic::apply<^^rejected_functions::bounded>(): "
              "rejected_functions::bounded has parameter `level` carrying a Scrydantic "
              "annotation, which a parameter cannot hold; take an argument aggregate "
              "and annotate its member");
static_assert(scrydantic::arguments_diagnostic<^^rejected_functions::fill>() ==
              "scrydantic::apply<^^rejected_functions::fill>(): "
              "rejected_functions::fill has parameter `total` of type int&, which the "
              "decoded arguments cannot bind; take it by value or by const reference");
static_assert(scrydantic::arguments_diagnostic<^^rejected_functions::gone>() ==
              "scrydantic::apply<^^rejected_functions::gone>(): "
              "rejected_functions::gone is deleted");
// The context is an argument where the binding has none, and misplaced where it
// has one.
static_assert(scrydantic::arguments_diagnostic<^^rejected_functions::late>() ==
              "scrydantic::apply<^^rejected_functions::late>(): "
              "rejected_functions::late has the unnamed parameter 2; an argument "
              "synthesized from a parameter is named after it");
static_assert(
    !scrydantic::arguments_diagnostic<^^rejected_functions::late, Session>().empty());

// Only a function has an argument object.
static_assert(scrydantic::arguments_diagnostic<^^rejected_functions::same>() ==
              "scrydantic::apply<^^rejected_functions::same>(): template<class Value> "
              "Value rejected_functions::same(Value) is a function template; a binding "
              "is of one function with one argument object");
static_assert(scrydantic::arguments_diagnostic<^^rejected_functions::limit>() ==
              "scrydantic::apply<^^rejected_functions::limit>(): "
              "rejected_functions::limit is not a function");
static_assert(scrydantic::arguments_diagnostic<^^rejected_functions>() ==
              "scrydantic::apply<^^rejected_functions>(): rejected_functions is not a "
              "function");
static_assert(!scrydantic::arguments_diagnostic<^^Till>().empty());

// ---- Annotated functions --------------------------------------------------------

// A library's own annotations: one that carries text, as scrydantic::tool does,
// and one that is a plain type.
namespace routes {

template <std::size_t Size> struct route {
  char path[Size]{};

  consteval route(const char (&text)[Size]) {
    for (std::size_t index = 0; index < Size; ++index) {
      path[index] = text[index];
    }
  }
};

struct internal {};
using hidden = internal;

} // namespace routes

namespace shop {

// A walk passes over what cannot carry an annotation, and over what carries
// another one or none.
template <typename Value> Value convert(Value value) { return value; }
namespace paths = routes;
struct Item {
  std::string name{};
};
inline constexpr std::int32_t stock_limit = 9;

[[= routes::route{"/items"}]] inline std::vector<std::string> list() {
  return {"bolt", "nut"};
}

inline std::int32_t untracked() { return 0; }

[[ = routes::route{"/items/{id}"},
   = routes::internal{} ]] inline std::string find(std::int32_t id) {
  return "item " + std::to_string(id);
}

[[= routes::internal{}]] inline void purge() {}

[[= scrydantic::tool{"Count the stock"}]] inline std::int32_t stock() { return 9; }

} // namespace shop

namespace {

class Desk {
public:
  [[= routes::internal{}]] void open() {}
  [[= routes::internal{}]] static void close() {}
  void idle() {}
  template <typename Visitor> void visit(Visitor) {}

private:
  [[= routes::internal{}]] void audit() {}
};

[[nodiscard]] consteval std::vector<std::string_view>
identifiers_of(const std::vector<std::meta::info>& functions) {
  std::vector<std::string_view> identifiers{};
  for (const auto function : functions) {
    identifiers.push_back(std::meta::identifier_of(function));
  }
  return identifiers;
}

} // namespace

// An annotation template matches each of its specializations, and an annotation
// type, under an alias too, matches itself. The order is declaration order.
static_assert(scrydantic::annotated_functions_of(^^shop, ^^routes::route) ==
              std::vector{^^shop::list, ^^shop::find});
static_assert(scrydantic::annotated_functions_of(^^shop, ^^routes::internal) ==
              std::vector{^^shop::find, ^^shop::purge});
static_assert(scrydantic::annotated_functions_of(^^shop, ^^routes::hidden) ==
              std::vector{^^shop::find, ^^shop::purge});
static_assert(scrydantic::annotated_functions_of(^^shop, ^^scrydantic::tool) ==
              std::vector{^^shop::stock});
static_assert(scrydantic::annotated_functions_of(^^routes, ^^routes::route).empty());
// A class's functions are found whatever their access, static or not.
static_assert(identifiers_of(scrydantic::annotated_functions_of(^^Desk,
                                                                ^^routes::internal)) ==
              std::vector<std::string_view>{"open", "close", "audit"});

// ---- A library on top ------------------------------------------------------------

// What a library that owns the `route` annotation builds from the pieces: every
// route of a namespace, each with its schema and a handler that decides for
// itself what the typed result becomes.
namespace routes {

struct Route {
  std::string_view name{};
  std::string_view schema{};
  std::string (*call)(const scrydantic::JsonView&){};
};

// One function as its caller's translation unit sees it, and the routes of one
// call. The library records its own identity as Scrydantic records a binding's.
template <std::meta::info Function, typename Binding> struct bound_route {};
template <typename... Bound> struct route_set {};

consteval std::meta::info routes_of(const std::meta::info scope) {
  std::vector<std::meta::info> bound{};
  for (const auto function : scrydantic::annotated_functions_of(scope, ^^route)) {
    bound.push_back(std::meta::substitute(
        ^^bound_route, {
                           std::meta::reflect_constant(function),
                           scrydantic::binding_of(function, ^^void)}));
  }
  return std::meta::substitute(^^route_set, bound);
}

// Text is the reply as it is; any other result is the reply as JSON.
inline std::string reply_of(std::string text) { return text; }

template <typename Result> std::string reply_of(const Result& result) {
  return scrydantic::encode(result).value().text;
}

template <std::meta::info Function, typename Binding>
Route route_of(bound_route<Function, Binding>) {
  using Arguments = scrydantic::arguments_t<Function, void, Binding>;
  return Route{.name = std::meta::identifier_of(Function),
               .schema = scrydantic::input_schema_v<Arguments>,
               .call = [](const scrydantic::JsonView& input) -> std::string {
                 auto arguments = scrydantic::decode<Arguments>(input);
                 if (!arguments) {
                   return "bad request: " + arguments.error().model_message;
                 }
                 return reply_of(
                     scrydantic::apply<Function, void, Binding>(std::move(*arguments)));
               }};
}

template <typename... Bound> std::vector<Route> routes_in(route_set<Bound...>) {
  return {route_of(Bound{})...};
}

template <std::meta::info Scope, typename Routes = typename[:routes_of(Scope):]>
std::vector<Route> add() {
  return routes_in(Routes{});
}

} // namespace routes

// The example of the guide's section for library authors: one function run as
// a command, whose result, a duration, has no JSON form.
namespace shell {

struct command {};

template <std::meta::info Function,
          typename Binding = typename[:scrydantic::binding_of(Function, ^^void):]>
std::string run(const scrydantic::JsonView& input) {
  constexpr std::string_view problem =
      scrydantic::arguments_diagnostic<Function>("shell::run");
  static_assert(problem.empty(), problem);
  if constexpr (!problem.empty()) {
    return {};
  } else {
    using Arguments = scrydantic::arguments_t<Function, void, Binding>;
    static constexpr std::string_view schema = scrydantic::input_schema_v<Arguments>;

    auto arguments = scrydantic::decode<Arguments>(input);
    if (!arguments) {
      return arguments.error().message + "; expected " + std::string{schema};
    }
    const std::chrono::seconds left =
        scrydantic::apply<Function, void, Binding>(std::move(*arguments));
    return std::to_string(left.count()) + " s";
  }
}

} // namespace shell

namespace timers {

[[= shell::command{}]] inline std::chrono::seconds remaining(std::string name,
                                                             std::int32_t laps) {
  return std::chrono::seconds{name == "tea" ? 90 : 60} * laps;
}

[[= shell::command{}]] inline std::chrono::seconds snooze() {
  return std::chrono::seconds{300};
}

struct Clock;
std::chrono::seconds reset(Clock* clock);

} // namespace timers

static_assert(scrydantic::arguments_diagnostic<^^timers::reset>("shell::run") ==
              "shell::run<^^timers::reset>(): timers::reset has parameter `clock`: "
              "timers::Clock* is not a supported reflected value");

namespace {

// The guide's listing of the commands of namespace timers: each with the number
// of keys its argument object has. A function template because GCC 16 reports
// -Wshadow for each expansion of a `template for` in a function that is not one.
template <typename Line = std::string>
[[nodiscard]] std::vector<Line> timer_commands() {
  std::vector<Line> listed{};
  template for (constexpr auto function : std::define_static_array(
                    scrydantic::annotated_functions_of(^^timers, ^^shell::command))) {
    listed.push_back(
        Line{std::meta::identifier_of(function)} + " " +
        std::to_string(scrydantic::keys_v<scrydantic::arguments_t<function>>.size()));
  }
  return listed;
}

} // namespace

// ---- Calls ---------------------------------------------------------------------

TEST_CASE("a function without an annotation is bound by hand") {
  SECTION("a parameter list, synthesized into an argument object") {
    using Arguments = scrydantic::arguments_t<^^ledger::label>;
    static constexpr std::string_view schema = scrydantic::input_schema_v<Arguments>;
    CHECK(schema == name_and_copies);

    const auto view = scrydantic::JsonView::parse(R"({"name":"bolt","copies":2})");
    REQUIRE(view);
    auto arguments = scrydantic::decode<Arguments>(*view);
    REQUIRE(arguments);
    CHECK(arguments->name == "bolt");
    CHECK(arguments->copies == 2);

    const std::string label = scrydantic::apply<^^ledger::label>(std::move(*arguments));
    CHECK(label == "boltx2");
  }

  SECTION("one aggregate, which is the argument object") {
    using Arguments = scrydantic::arguments_t<^^ledger::width>;
    static constexpr std::string_view schema = scrydantic::input_schema_v<Arguments>;
    CHECK(schema == scrydantic::input_schema_v<Span>);

    const auto view = scrydantic::JsonView::parse(R"({"first":2,"last":9})");
    REQUIRE(view);
    auto arguments = scrydantic::decode<Arguments>(*view);
    REQUIRE(arguments);

    const std::int32_t width =
        scrydantic::apply<^^ledger::width>(std::move(*arguments));
    CHECK(width == 7);
  }

  SECTION("no parameters, which take an empty object") {
    using Arguments = scrydantic::arguments_t<^^ledger::count>;
    static constexpr std::string_view schema = scrydantic::input_schema_v<Arguments>;
    CHECK(schema == no_members);

    const auto view = scrydantic::JsonView::parse("{}");
    REQUIRE(view);
    auto arguments = scrydantic::decode<Arguments>(*view);
    REQUIRE(arguments);

    const std::int32_t count =
        scrydantic::apply<^^ledger::count>(std::move(*arguments));
    CHECK(count == 3);
  }
}

TEST_CASE("arguments that do not fit the function fail to decode, before any call") {
  const auto synthesized = scrydantic::decode<scrydantic::arguments_t<^^ledger::label>>(
      R"({"name":7,"count":2})");
  REQUIRE_FALSE(synthesized);
  CHECK(synthesized.error().model_message == "$ contains unknown member \"count\"\n"
                                             "$.name must be a string\n"
                                             "$.copies is a required member");

  const auto none =
      scrydantic::decode<scrydantic::arguments_t<^^ledger::count>>(R"({"extra":1})");
  REQUIRE_FALSE(none);
  CHECK(none.error().model_message == "$ contains unknown member \"extra\"");
}

TEST_CASE("a context reaches a bound function that takes it first") {
  const Session session{.user = "ada"};

  CHECK(scrydantic::apply<^^ledger::owner, Session>(
            session, decoded<^^ledger::owner, Session>("{}")) == "ada");
  CHECK(scrydantic::apply<^^ledger::claim, Session>(
            session, decoded<^^ledger::claim, Session>(R"({"first":1,"last":4})")) ==
        "ada:3");
  CHECK(scrydantic::apply<^^ledger::sign, Session>(
            session, decoded<^^ledger::sign, Session>(
                         R"({"note":"paid","loud":true})")) == "ada!paid");

  // A function that takes no context is called the same way, without it.
  CHECK(scrydantic::apply<^^ledger::label, Session>(
            session, decoded<^^ledger::label, Session>(
                         R"({"name":"nut","copies":3})")) == "nutx3");
  CHECK(scrydantic::apply<^^ledger::count, Session>(
            session, decoded<^^ledger::count, Session>("{}")) == 3);
}

TEST_CASE("a static member function is bound like a free function") {
  const Session session{.user = "ada"};

  CHECK(scrydantic::apply<^^Till::limit>(decoded<^^Till::limit>("{}")) == 100);
  CHECK(scrydantic::apply<^^Till::width>(
            decoded<^^Till::width>(R"({"first":2,"last":9})")) == 7);
  CHECK(scrydantic::apply<^^Till::label>(
            decoded<^^Till::label>(R"({"name":"bolt","copies":2})")) == "boltx2");

  CHECK(scrydantic::apply<^^Till::owner, Session>(
            session, decoded<^^Till::owner, Session>("{}")) == "ada");
  CHECK(scrydantic::apply<^^Till::claim, Session>(
            session, decoded<^^Till::claim, Session>(R"({"first":1,"last":4})")) ==
        "ada:3");
  CHECK(scrydantic::apply<^^Till::sign, Session>(
            session, decoded<^^Till::sign, Session>(
                         R"({"note":"paid","loud":false})")) == "ada.paid");

  // Given an object all the same, it ignores it, so a library can call every
  // function of a class one way.
  Till till{};
  CHECK(scrydantic::apply<^^Till::limit>(till, decoded<^^Till::limit>("{}")) == 100);
  CHECK(scrydantic::apply<^^Till::owner, Session>(
            till, session, decoded<^^Till::owner, Session>("{}")) == "ada");
}

TEST_CASE("a member function is called on an object") {
  Till till{};

  CHECK(scrydantic::apply<^^Till::add>(
            till, decoded<^^Till::add>(R"({"first":2,"last":9})")) == 7);
  CHECK(scrydantic::apply<^^Till::total>(till, decoded<^^Till::total>("{}")) == 7);
  CHECK(scrydantic::apply<^^Till::stamp>(
            till, decoded<^^Till::stamp>(R"({"note":"ok","copies":1})")) ==
        "till:okx8");

  SECTION("with a context") {
    const Session session{.user = "ada"};
    CHECK(scrydantic::apply<^^Till::teller, Session>(
              till, session, decoded<^^Till::teller, Session>("{}")) == "ada@till");
    CHECK(scrydantic::apply<^^Till::credit, Session>(
              till, session,
              decoded<^^Till::credit, Session>(R"({"first":0,"last":2})")) == 13);
    CHECK(scrydantic::apply<^^Till::memo, Session>(
              till, session,
              decoded<^^Till::memo, Session>(R"({"note":"paid","loud":true})")) ==
          "ada@till!paid");
    // Without a context of its own, a member function is called without one.
    CHECK(scrydantic::apply<^^Till::total, Session>(
              till, session, decoded<^^Till::total, Session>("{}")) == 13);
  }

  SECTION("a const object calls a const member function") {
    const Till& read_only = till;
    CHECK(scrydantic::apply<^^Till::total>(read_only, decoded<^^Till::total>("{}")) ==
          7);
    CHECK(scrydantic::apply<^^Till::stamp>(
              read_only, decoded<^^Till::stamp>(R"({"note":"ok","copies":0})")) ==
          "till:okx7");
  }

  SECTION("an object of a derived class calls a member function of its base") {
    NightTill night{};
    CHECK(scrydantic::apply<^^Till::add>(
              night, decoded<^^Till::add>(R"({"first":0,"last":5})")) == 5);
    CHECK(night.total() == 5);
  }
}

TEST_CASE("an explicit object parameter is initialized from the object") {
  Till till{};
  const Session session{.user = "ada"};

  CHECK(scrydantic::apply<^^Till::raised>(till,
                                          decoded<^^Till::raised>(R"({"by":4})")) == 4);
  CHECK(till.total() == 4);

  const Till& read_only = till;
  CHECK(scrydantic::apply<^^Till::scaled>(
            read_only, decoded<^^Till::scaled>(R"({"factor":3})")) == 12);
  CHECK(scrydantic::apply<^^Till::seen, Session>(
            read_only, session, decoded<^^Till::seen, Session>(R"({"times":2})")) ==
        "ada@tillx2");

  // By value, the function is called on a copy.
  CHECK(scrydantic::apply<^^Till::trial>(read_only,
                                         decoded<^^Till::trial>(R"({"by":10})")) == 14);
  CHECK(till.total() == 4);
}

TEST_CASE("apply returns exactly what the function returns") {
  SECTION("a Result, with the function's own error") {
    const scrydantic::Result<std::int32_t> halved =
        scrydantic::apply<^^ledger::halve>(decoded<^^ledger::halve>(R"({"value":8})"));
    REQUIRE(halved);
    CHECK(*halved == 4);

    const scrydantic::Result<std::int32_t> refused =
        scrydantic::apply<^^ledger::halve>(decoded<^^ledger::halve>(R"({"value":7})"));
    REQUIRE_FALSE(refused);
    CHECK(refused.error().model_message == "must be even");
  }

  SECTION("a move-only value that has no JSON form") {
    Lease lease =
        scrydantic::apply<^^ledger::lease>(decoded<^^ledger::lease>(R"({"slot":12})"));
    REQUIRE(lease.slot);
    CHECK(*lease.slot == 12);
  }

  SECTION("nothing") {
    Till till{};
    scrydantic::apply<^^Till::add>(till,
                                   decoded<^^Till::add>(R"({"first":0,"last":5})"));
    scrydantic::apply<^^Till::reset>(till, decoded<^^Till::reset>("{}"));
    CHECK(till.total() == 0);
  }

  SECTION("a reference, which still refers to the object") {
    const Till till{};
    const std::string& title =
        scrydantic::apply<^^Till::title>(till, decoded<^^Till::title>("{}"));
    CHECK(&title == &till.title());
  }
}

TEST_CASE("each argument is moved into its parameter") {
  auto arguments = decoded<^^ledger::join>(
      R"({"head":"a head long enough to own its storage, ","tail":"and a tail"})");

  CHECK(scrydantic::apply<^^ledger::join>(std::move(arguments)) ==
        "a head long enough to own its storage, and a tail");
  // The function took the first as an rvalue and emptied it, and read the second
  // in place.
  // NOLINTNEXTLINE(bugprone-use-after-move): what the call left behind is the point.
  CHECK(arguments.head.empty());
  CHECK(arguments.tail == "and a tail");
}

TEST_CASE("the guide's command runner reads, calls, and prints by its own rules") {
  const auto input = scrydantic::JsonView::parse(R"({"name":"tea","laps":2})");
  const auto short_input = scrydantic::JsonView::parse(R"({"name":"tea"})");
  REQUIRE(input);
  REQUIRE(short_input);

  CHECK(shell::run<^^timers::remaining>(*input) == "180 s");
  CHECK(shell::run<^^timers::remaining>(*short_input) ==
        R"($.laps is a required member; expected {"additionalProperties":false,)"
        R"("properties":{"laps":{"maximum":2147483647,"minimum":-2147483648,)"
        R"("type":"integer"},"name":{"type":"string"}},"required":["laps","name"],)"
        R"("type":"object"})");

  CHECK(timer_commands() == std::vector<std::string>{"remaining 2", "snooze 0"});
}

TEST_CASE("a library binds every function that carries its own annotation") {
  const std::vector<routes::Route> added = routes::add<^^shop>();

  REQUIRE(added.size() == 2);
  CHECK(added[0].name == "list");
  CHECK(added[0].schema == no_members);
  CHECK(added[1].name == "find");
  CHECK(
      added[1].schema ==
      R"({"additionalProperties":false,"properties":{"id":{"maximum":2147483647,)"
      R"("minimum":-2147483648,"type":"integer"}},"required":["id"],"type":"object"})");

  const auto nothing = scrydantic::JsonView::parse("{}");
  const auto seven = scrydantic::JsonView::parse(R"({"id":7})");
  REQUIRE(nothing);
  REQUIRE(seven);
  // The library chose each reply from the typed result: JSON for a list, the
  // text itself for a string.
  CHECK(added[0].call(*nothing) == R"(["bolt","nut"])");
  CHECK(added[1].call(*seven) == "item 7");
  CHECK(added[1].call(*nothing) == "bad request: $.id is a required member");
}
