// Parameters that the host supplies. A specialization of scrydantic::supplier
// lets a binding's context fill a parameter of one type, wherever it stands in
// the parameter list. Such a parameter is no member of the argument object and
// is in no schema, and a supplier that returns a failed Result refuses the call
// before the function runs.

#include <array>
#include <catch2/catch_test_macros.hpp>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <functional>
#include <memory>
#include <meta>
#include <scrydantic/scrydantic.hpp>
#include <stop_token>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

namespace {

// What a host hands its tools: a class that the codec cannot read and that
// cannot be copied, so a function can take it by reference only.
class Database {
public:
  explicit Database(const std::int32_t rows) : rows_{rows} {}
  Database(const Database&) = delete;
  Database& operator=(const Database&) = delete;
  Database(Database&&) = delete;
  Database& operator=(Database&&) = delete;
  ~Database() = default;

  [[nodiscard]] std::int32_t rows() const { return rows_; }

private:
  std::int32_t rows_;
};

// A plain aggregate: supplied by value where the context supplies it, and an
// argument that the codec reads where nothing does.
struct Clock {
  std::int32_t now{};
};

// State of the host's that a function changes through a reference.
struct Journal {
  std::vector<std::string> lines{};
};

// A permission and an allowance, which their suppliers may refuse.
struct Admin {
  std::string name{};
};

struct Quota {
  std::int32_t left{};
};

// Something the host may not have, handed out by reference when it does.
struct Vault {
  std::int32_t coins{};
};

// An argument object that a function takes whole.
struct Span {
  std::int32_t first{};
  std::int32_t last{};
};

// The host's call context. `trace` records what ran, in order: `a` and `q` for
// the suppliers of Admin and Quota, and `f` for a function that writes it.
struct Session {
  std::string user{};
  const Database* database{};
  std::stop_token stop{};
  std::int32_t now{};
  std::int32_t quota{1};
  Journal* journal{};
  Vault* vault{};
  std::string* trace{};
};

// A context for which nothing is supplied.
struct Guest {};

void traced(const Session& session, const char step) {
  if (session.trace != nullptr) {
    session.trace->push_back(step);
  }
}

} // namespace

// A reference to something the context holds.
template <> struct scrydantic::supplier<Database, Session> {
  static const Database& supply(const Session& session) { return *session.database; }
};

// A value.
template <> struct scrydantic::supplier<std::stop_token, Session> {
  static std::stop_token supply(const Session& session) { return session.stop; }
};

template <> struct scrydantic::supplier<Clock, Session> {
  static Clock supply(const Session& session) { return Clock{.now = session.now}; }
};

// A reference to state that the function may change.
template <> struct scrydantic::supplier<Journal, Session> {
  static Journal& supply(const Session& session) { return *session.journal; }
};

// Suppliers that may refuse.
template <> struct scrydantic::supplier<Admin, Session> {
  static scrydantic::Result<Admin> supply(const Session& session) {
    traced(session, 'a');
    if (session.user != "ada") {
      return std::unexpected(scrydantic::Error{
          .message = "caller " + session.user + " is no administrator",
          .model_message = "only an administrator may do this",
      });
    }
    return Admin{.name = session.user};
  }
};

template <> struct scrydantic::supplier<Quota, Session> {
  static scrydantic::Result<Quota> supply(const Session& session) {
    traced(session, 'q');
    if (session.quota <= 0) {
      return std::unexpected(scrydantic::tool_error("the quota is used up"));
    }
    return Quota{.left = session.quota};
  }
};

// One that may refuse and hands out a reference: a Result holds no reference,
// and a reference_wrapper converts to one.
template <> struct scrydantic::supplier<Vault, Session> {
  static scrydantic::Result<std::reference_wrapper<Vault>>
  supply(const Session& session) {
    if (session.vault == nullptr) {
      return std::unexpected(scrydantic::tool_error("there is no vault"));
    }
    return std::ref(*session.vault);
  }
};

namespace {

struct Report {
  std::string name{};
  std::int32_t rows{};
  bool stopped{};
};

} // namespace

// The example of the issue: the model supplies `name`, the host `db` and `stop`.
namespace report_tools {

[[= scrydantic::tool{"Export the report"}]] inline Report
    export_report(std::string name, const Database& db, std::stop_token stop) {
  return Report{
      .name = std::move(name), .rows = db.rows(), .stopped = stop.stop_requested()};
}

} // namespace report_tools

// The examples of the guide's Call context paragraph, with a context of their
// own.
namespace {

struct Desk {
  std::string user{};
  const Database* database{};
  std::stop_token stop{};
};

} // namespace

template <> struct scrydantic::supplier<Database, Desk> {
  static const Database& supply(const Desk& desk) { return *desk.database; }
};

template <> struct scrydantic::supplier<std::stop_token, Desk> {
  static std::stop_token supply(const Desk& desk) { return desk.stop; }
};

template <> struct scrydantic::supplier<Admin, Desk> {
  static scrydantic::Result<Admin> supply(const Desk& desk) {
    if (desk.user != "ada") {
      return std::unexpected(
          scrydantic::tool_error("only an administrator may do this"));
    }
    return Admin{desk.user};
  }
};

namespace desk_tools {

[[= scrydantic::tool{"Export the report"}]] inline Report
    export_report(std::string name, const Database& db, std::stop_token stop) {
  return report_tools::export_report(std::move(name), db, std::move(stop));
}

inline std::vector<std::string> deleted{};

[[= scrydantic::tool{"Delete a report"}]] inline void delete_report(std::string name,
                                                                    Admin admin) {
  deleted.push_back(admin.name + ":" + name);
}

} // namespace desk_tools

// Supplied parameters before, between, and after the arguments, around them
// all, after the context itself, alone, and beside a lone aggregate.
namespace placed_tools {

[[= scrydantic::tool{"Supplied first"}]] inline std::string
    before(const Database& db, std::string name, std::int32_t copies) {
  return name + "x" + std::to_string(copies) + "/" + std::to_string(db.rows());
}

[[= scrydantic::tool{"Supplied in the middle"}]] inline std::string
    between(std::string name, const Database& db, std::int32_t copies) {
  return name + "x" + std::to_string(copies) + "/" + std::to_string(db.rows());
}

[[= scrydantic::tool{"Supplied last"}]] inline std::string
    after(std::string name, std::int32_t copies, const Database& db) {
  return name + "x" + std::to_string(copies) + "/" + std::to_string(db.rows());
}

[[= scrydantic::tool{"Supplied all around"}]] inline std::string
    around(Clock clock, std::string name, const Database& db, std::int32_t copies,
           std::stop_token stop) {
  return name + "x" + std::to_string(copies) + "/" + std::to_string(db.rows()) + "@" +
         std::to_string(clock.now) + (stop.stop_requested() ? "!" : ".");
}

[[= scrydantic::tool{"Supplied after the context"}]] inline std::string
    led(const Session& session, std::string name, const Database& db) {
  return session.user + ":" + name + "/" + std::to_string(db.rows());
}

[[= scrydantic::tool{"Nothing but supplied parameters"}]] inline std::int32_t
    only(const Database& db, Clock clock) {
  return db.rows() + clock.now;
}

[[= scrydantic::tool{
    "A whole argument object among supplied parameters"}]] inline std::int32_t
    whole(const Database& db, Span span, Clock clock) {
  return db.rows() + clock.now + span.last - span.first;
}

} // namespace placed_tools

// Default arguments beside supplied parameters.
namespace defaulted_tools {

[[= scrydantic::tool{"A default after a supplied parameter"}]] inline std::string
    limited(std::string name, const Database& db, std::int32_t limit = 10) {
  return name + "/" + std::to_string(db.rows()) + "<" + std::to_string(limit);
}

[[= scrydantic::tool{"Several defaults after supplied parameters"}]] inline std::string
    paged(const Database& db, std::string name, Clock clock, std::int32_t limit = 10,
          std::int32_t page = 1) {
  return name + "/" + std::to_string(db.rows()) + "@" + std::to_string(clock.now) +
         "<" + std::to_string(limit) + "#" + std::to_string(page);
}

// A supplied parameter with a default argument of its own: the supplier wins.
[[= scrydantic::tool{"A supplied parameter with a default"}]] inline std::int32_t
    stamped(std::int32_t offset, Clock clock = Clock{.now = -100}) {
  return offset + clock.now;
}

} // namespace defaulted_tools

// What a supplied parameter may be declared as.
namespace passing_tools {

[[= scrydantic::tool{"A value, by value"}]] inline std::int32_t by_value(Clock clock) {
  return clock.now;
}

[[= scrydantic::tool{"A value, by const reference"}]] inline std::int32_t
    by_const_reference(const Clock& clock) {
  return clock.now;
}

[[= scrydantic::tool{"A value, by rvalue reference"}]] inline std::int32_t
    by_rvalue_reference(Clock&& clock) {
  return clock.now;
}

[[= scrydantic::tool{"A reference, by reference"}]] inline std::int32_t
    write(Journal& journal, std::string line) {
  journal.lines.push_back(std::move(line));
  return static_cast<std::int32_t>(journal.lines.size());
}

[[= scrydantic::tool{"A reference, by value"}]] inline std::int32_t
    copy(Journal journal, std::string line) {
  journal.lines.push_back(std::move(line));
  return static_cast<std::int32_t>(journal.lines.size());
}

[[= scrydantic::tool{"A wrapped reference, by reference"}]] inline std::int32_t
    deposit(Vault& vault, std::int32_t coins) {
  vault.coins += coins;
  return vault.coins;
}

} // namespace passing_tools

// Functions whose suppliers may refuse.
namespace guarded_tools {

[[= scrydantic::tool{"Purge a report"}]] inline std::string
    purge(const Session& session, std::string name, Admin admin, Quota quota) {
  traced(session, 'f');
  return admin.name + " purged " + name + " with " + std::to_string(quota.left);
}

[[= scrydantic::tool{"Lock the reports"}]] inline void lock(const Session& session,
                                                            Admin admin) {
  static_cast<void>(admin);
  traced(session, 'f');
}

[[= scrydantic::tool{
    "Halve a count, as an administrator"}]] inline scrydantic::Result<std::int32_t>
    halve(Admin admin, std::int32_t count) {
  static_cast<void>(admin);
  if (count % 2 != 0) {
    return std::unexpected(scrydantic::tool_error("count is odd"));
  }
  return count / 2;
}

} // namespace guarded_tools

// No tools: what these return is apply()'s business alone.
namespace handed {

inline const Database& same(const Database& db) { return db; }
inline Vault& opened(Vault& vault) { return vault; }

} // namespace handed

// A function that takes a type that only some contexts supply.
namespace clock_tools {

[[= scrydantic::tool{"Add the time"}]] inline std::int32_t timed(std::int32_t offset,
                                                                 Clock clock) {
  return offset + clock.now;
}

} // namespace clock_tools

namespace {

// A toolbox whose member functions take supplied parameters.
class Archive {
public:
  [[= scrydantic::tool{"Fetch a record"}]] std::string fetch(std::string key,
                                                             const Database& db) const {
    return prefix_ + key + "/" + std::to_string(db.rows());
  }

  [[= scrydantic::tool{"Note a line"}]] std::int32_t note(Journal& journal,
                                                          std::string line) {
    journal.lines.push_back(std::move(line));
    return ++notes_;
  }

  [[= scrydantic::tool{"Count the notes"}]] std::int32_t
      noted(this const Archive& self, const Session& session, Clock clock) {
    return self.notes_ + clock.now + static_cast<std::int32_t>(session.user.size());
  }

private:
  std::string prefix_{"rec:"};
  std::int32_t notes_{};
};

// A class whose member functions apply() and try_apply() call on an object.
class Shelf {
public:
  std::string fetch(std::string key, const Database& db) const {
    return "shelf:" + key + "/" + std::to_string(db.rows());
  }

  std::int32_t stock(Admin admin, std::int32_t count) {
    static_cast<void>(admin);
    return stocked_ += count;
  }

private:
  std::int32_t stocked_{};
};

// The argument object of `Function` in a binding with `Context`, decoded.
// `Binding` is forwarded, as the header of tools.hpp asks of a wrapper.
template <std::meta::info Function, typename Context = void,
          typename Binding = typename[:scrydantic::binding_of(Function, ^^Context):]>
[[nodiscard]] scrydantic::arguments_t<Function, Context, Binding>
decoded(const std::string_view text) {
  auto arguments =
      scrydantic::decode<scrydantic::arguments_t<Function, Context, Binding>>(text);
  REQUIRE(arguments);
  return std::move(*arguments);
}

[[nodiscard]] scrydantic::Json json(const std::string_view text) {
  return scrydantic::Json{.text = std::string{text}};
}

constexpr std::string_view name_alone =
    R"({"additionalProperties":false,"properties":{"name":{"type":"string"}},)"
    R"("required":["name"],"type":"object"})";

constexpr std::string_view no_members =
    R"({"additionalProperties":false,"properties":{},"required":[],"type":"object"})";

template <typename Arguments> [[nodiscard]] Arguments&& given();

} // namespace

// ---- The argument object leaves supplied parameters out ----------------------------

static_assert(scrydantic::input_schema_v<
                  scrydantic::arguments_t<^^report_tools::export_report, Session>> ==
              name_alone);
static_assert(
    scrydantic::keys_v<scrydantic::arguments_t<^^placed_tools::before, Session>> ==
    std::array<std::string_view, 2>{"copies", "name"});
static_assert(
    scrydantic::keys_v<scrydantic::arguments_t<^^placed_tools::around, Session>> ==
    std::array<std::string_view, 2>{"copies", "name"});
static_assert(
    scrydantic::keys_v<scrydantic::arguments_t<^^placed_tools::led, Session>> ==
    std::array<std::string_view, 1>{"name"});
// Nothing but supplied parameters leaves an object without members, and a lone
// aggregate among them is the object itself.
static_assert(std::is_empty_v<scrydantic::arguments_t<^^placed_tools::only, Session>>);
static_assert(
    std::same_as<scrydantic::arguments_t<^^placed_tools::whole, Session>, Span>);
// A supplied parameter with a default argument is no optional member: it is no
// member at all.
static_assert(
    scrydantic::keys_v<scrydantic::arguments_t<^^defaulted_tools::stamped, Session>> ==
    std::array<std::string_view, 1>{"offset"});

// Without a context, and with one that has no supplier for the type, the same
// parameter is an argument.
static_assert(scrydantic::keys_v<scrydantic::arguments_t<^^clock_tools::timed>> ==
              std::array<std::string_view, 2>{"clock", "offset"});
static_assert(
    scrydantic::keys_v<scrydantic::arguments_t<^^clock_tools::timed, Guest>> ==
    std::array<std::string_view, 2>{"clock", "offset"});
static_assert(
    scrydantic::keys_v<scrydantic::arguments_t<^^clock_tools::timed, Session>> ==
    std::array<std::string_view, 1>{"offset"});

// describe() publishes the schema of that same object.
static_assert(scrydantic::describe<^^report_tools, Session>()[0].input_schema ==
              name_alone);
static_assert(scrydantic::describe<^^placed_tools, Session>()[5].input_schema ==
              no_members);
static_assert(scrydantic::describe<^^placed_tools, Session>()[6].input_schema ==
              scrydantic::input_schema_v<Span>);
static_assert(scrydantic::describe<Archive, Session>()[0].input_schema ==
              R"({"additionalProperties":false,"properties":{"key":{"type":"string"}},)"
              R"("required":["key"],"type":"object"})");
static_assert(
    scrydantic::input_schema_v<scrydantic::arguments_t<^^Shelf::fetch, Session>> ==
    scrydantic::describe<Archive, Session>()[0].input_schema);
static_assert(
    scrydantic::describe<^^clock_tools>()[0].input_schema ==
    scrydantic::input_schema_v<scrydantic::arguments_t<^^clock_tools::timed>>);

// ---- What apply() and try_apply() return, at compile time ------------------------

// apply() returns exactly what the function returns.
static_assert(std::same_as<
              decltype(scrydantic::apply<^^placed_tools::between, Session>(
                  given<const Session&>(),
                  given<scrydantic::arguments_t<^^placed_tools::between, Session>>())),
              std::string>);
static_assert(std::same_as<decltype(scrydantic::apply<^^handed::same, Session>(
                               given<const Session&>(), {})),
                           const Database&>);

// try_apply() returns a Result of it: a Status for void, a Result of the Result
// a function returns, and a reference_wrapper for a reference.
static_assert(std::same_as<
              decltype(scrydantic::try_apply<^^guarded_tools::purge, Session>(
                  given<const Session&>(),
                  given<scrydantic::arguments_t<^^guarded_tools::purge, Session>>())),
              scrydantic::Result<std::string>>);
static_assert(
    std::same_as<decltype(scrydantic::try_apply<^^guarded_tools::lock, Session>(
                     given<const Session&>(), {})),
                 scrydantic::Status>);
static_assert(std::same_as<
              decltype(scrydantic::try_apply<^^guarded_tools::halve, Session>(
                  given<const Session&>(),
                  given<scrydantic::arguments_t<^^guarded_tools::halve, Session>>())),
              scrydantic::Result<scrydantic::Result<std::int32_t>>>);
static_assert(std::same_as<decltype(scrydantic::try_apply<^^handed::opened, Session>(
                               given<const Session&>(), {})),
                           scrydantic::Result<std::reference_wrapper<Vault>>>);

// ---- Diagnostics -------------------------------------------------------------------

namespace unsupplied {

inline std::int32_t deref(std::string name, const std::int32_t* cursor) {
  return static_cast<std::int32_t>(name.size()) + *cursor;
}

} // namespace unsupplied

static_assert(
    scrydantic::arguments_diagnostic<^^report_tools::export_report, Session>().empty());
static_assert(
    scrydantic::arguments_diagnostic<^^placed_tools::whole, Session>().empty());
static_assert(
    scrydantic::arguments_diagnostic<^^defaulted_tools::paged, Session>().empty());
static_assert(
    scrydantic::arguments_diagnostic<^^passing_tools::write, Session>().empty());
static_assert(
    scrydantic::arguments_diagnostic<^^passing_tools::deposit, Session>().empty());
// A Toolbox with supplied parameters is one only with the context that supplies
// them.
static_assert(scrydantic::Toolbox<Archive, Session>);
static_assert(!scrydantic::Toolbox<Archive>);

// A binding without a context has no supplier to speak of, so its rejection of
// a type that the codec cannot read is worded as before.
static_assert(scrydantic::arguments_diagnostic<^^unsupplied::deref>() ==
              "scrydantic::apply<^^unsupplied::deref>(): unsupplied::deref has "
              "parameter `cursor`: const int* is not a supported reflected value");
static_assert(scrydantic::arguments_diagnostic<^^unsupplied::deref, Guest>().ends_with(
    ": const int* is not a supported reflected value, and "
    "scrydantic::supplier<const int*, {anonymous}::Guest> is not defined, so the "
    "context does not supply it"));
// What no context supplies and the codec cannot read is no argument either.
static_assert(
    !scrydantic::arguments_diagnostic<^^report_tools::export_report>().empty());
static_assert(
    !scrydantic::arguments_diagnostic<^^report_tools::export_report, Guest>().empty());

// ---- Calls -------------------------------------------------------------------------

TEST_CASE("a tool receives what the model supplies and what the host supplies") {
  const Database database{42};
  std::stop_source source{};
  const Session session{.database = &database, .stop = source.get_token()};

  const auto tools = scrydantic::bind<^^report_tools, Session>();
  REQUIRE(tools.size() == 1);
  CHECK(tools[0].input_schema.text == name_alone);

  auto report = tools[0].invoke(session, json(R"({"name":"q3"})"));
  REQUIRE(report);
  CHECK(report->text == R"({"name":"q3","rows":42,"stopped":false})");

  // The token is the session's own.
  source.request_stop();
  report = tools[0].invoke(session, json(R"({"name":"q3"})"));
  REQUIRE(report);
  CHECK(report->text == R"({"name":"q3","rows":42,"stopped":true})");

  // A supplied parameter is no argument, so the model cannot give it.
  const auto forged = tools[0].invoke(session, json(R"({"name":"q3","db":{}})"));
  REQUIRE_FALSE(forged);
  CHECK(forged.error().model_message == "$ contains unknown member \"db\"");
}

TEST_CASE("a supplied parameter stands anywhere in the parameter list") {
  const Database database{7};
  std::stop_source source{};
  source.request_stop();
  const Session session{
      .user = "ada", .database = &database, .stop = source.get_token(), .now = 5};

  const auto tools = scrydantic::bind<^^placed_tools, Session>();
  REQUIRE(tools.size() == 7);
  const auto call = [&](const std::size_t index, const std::string_view arguments) {
    const auto result = tools[index].invoke(session, json(arguments));
    REQUIRE(result);
    return result->text;
  };

  CHECK(call(0, R"({"name":"nut","copies":3})") == R"("nutx3/7")");
  CHECK(call(1, R"({"name":"nut","copies":3})") == R"("nutx3/7")");
  CHECK(call(2, R"({"name":"nut","copies":3})") == R"("nutx3/7")");
  CHECK(call(3, R"({"name":"nut","copies":3})") == R"("nutx3/7@5!")");
  CHECK(call(4, R"({"name":"nut"})") == R"("ada:nut/7")");
  CHECK(call(5, "{}") == "12");
  CHECK(call(6, R"({"first":2,"last":9})") == "19");

  // Every tool publishes the schema of the object its handler decodes.
  constexpr auto described = scrydantic::describe<^^placed_tools, Session>();
  for (std::size_t index = 0; index < tools.size(); ++index) {
    CHECK(tools[index].input_schema.text == described[index].input_schema);
  }
}

TEST_CASE("apply() fills in the supplied parameters") {
  const Database database{7};
  const Session session{.user = "ada", .database = &database, .now = 5};

  CHECK(scrydantic::apply<^^placed_tools::before, Session>(
            session, decoded<^^placed_tools::before, Session>(
                         R"({"name":"nut","copies":3})")) == "nutx3/7");
  CHECK(scrydantic::apply<^^placed_tools::between, Session>(
            session, decoded<^^placed_tools::between, Session>(
                         R"({"name":"nut","copies":3})")) == "nutx3/7");
  CHECK(scrydantic::apply<^^placed_tools::after, Session>(
            session, decoded<^^placed_tools::after, Session>(
                         R"({"name":"nut","copies":3})")) == "nutx3/7");
  CHECK(scrydantic::apply<^^placed_tools::around, Session>(
            session, decoded<^^placed_tools::around, Session>(
                         R"({"name":"nut","copies":3})")) == "nutx3/7@5.");
  CHECK(scrydantic::apply<^^placed_tools::led, Session>(
            session, decoded<^^placed_tools::led, Session>(R"({"name":"nut"})")) ==
        "ada:nut/7");
  CHECK(scrydantic::apply<^^placed_tools::only, Session>(session, {}) == 12);
  CHECK(scrydantic::apply<^^placed_tools::whole, Session>(
            session, Span{.first = 2, .last = 9}) == 19);

  // A reference that a supplier returned reaches the function as that object.
  CHECK(&scrydantic::apply<^^handed::same, Session>(session, {}) == &database);
}

TEST_CASE("a default argument applies beside supplied parameters") {
  const Database database{7};
  const Session session{.database = &database, .now = 5};

  const auto tools = scrydantic::bind<^^defaulted_tools, Session>();
  REQUIRE(tools.size() == 3);
  const auto call = [&](const std::size_t index, const std::string_view arguments) {
    const auto result = tools[index].invoke(session, json(arguments));
    REQUIRE(result);
    return result->text;
  };

  CHECK(call(0, R"({"name":"nut"})") == R"("nut/7<10")");
  CHECK(call(0, R"({"name":"nut","limit":3})") == R"("nut/7<3")");

  // Each prefix of the defaulted arguments is a call of its own.
  CHECK(call(1, R"({"name":"nut"})") == R"("nut/7@5<10#1")");
  CHECK(call(1, R"({"name":"nut","limit":3})") == R"("nut/7@5<3#1")");
  CHECK(call(1, R"({"name":"nut","limit":3,"page":4})") == R"("nut/7@5<3#4")");
  const auto gap = tools[1].invoke(session, json(R"({"name":"nut","page":4})"));
  REQUIRE_FALSE(gap);
  CHECK(gap.error().model_message == "$.limit is required when \"page\" is given");

  // The supplier wins over the parameter's own default argument.
  CHECK(call(2, R"({"offset":1})") == "6");
  const auto given =
      tools[2].invoke(session, json(R"({"offset":1,"clock":{"now":9}})"));
  REQUIRE_FALSE(given);
  CHECK(given.error().model_message == "$ contains unknown member \"clock\"");

  CHECK(scrydantic::apply<^^defaulted_tools::limited, Session>(
            session, decoded<^^defaulted_tools::limited, Session>(
                         R"({"name":"nut"})")) == "nut/7<10");
  CHECK(scrydantic::apply<^^defaulted_tools::paged, Session>(
            session, decoded<^^defaulted_tools::paged, Session>(
                         R"({"name":"nut","limit":3})")) == "nut/7@5<3#1");
}

TEST_CASE("what a supplier returns initializes the parameter") {
  Journal journal{.lines = {"first"}};
  Vault vault{.coins = 10};
  const Session session{.now = 5, .journal = &journal, .vault = &vault};

  const auto tools = scrydantic::bind<^^passing_tools, Session>();
  REQUIRE(tools.size() == 6);
  const auto call = [&](const std::size_t index, const std::string_view arguments) {
    const auto result = tools[index].invoke(session, json(arguments));
    REQUIRE(result);
    return result->text;
  };

  // A value fills a parameter taken by value, by const reference, or by rvalue
  // reference.
  CHECK(call(0, "{}") == "5");
  CHECK(call(1, "{}") == "5");
  CHECK(call(2, "{}") == "5");

  // A reference is the host's own object, which the function changes.
  CHECK(call(3, R"({"line":"second"})") == "2");
  CHECK(journal.lines == std::vector<std::string>{"first", "second"});

  // Taken by value, it is a copy.
  CHECK(call(4, R"({"line":"third"})") == "3");
  CHECK(journal.lines.size() == 2);

  // A reference_wrapper in a Result fills a reference parameter.
  CHECK(call(5, R"({"coins":5})") == "15");
  CHECK(vault.coins == 15);
}

TEST_CASE("a supplier that refuses ends the call before the function runs") {
  std::string trace{};
  const auto tools = scrydantic::bind<^^guarded_tools, Session>();
  REQUIRE(tools.size() == 3);

  SECTION(
      "every supplier runs, in the order of the parameters, and then the function") {
    const Session session{.user = "ada", .quota = 2, .trace = &trace};
    const auto purged = tools[0].invoke(session, json(R"({"name":"q3"})"));
    REQUIRE(purged);
    CHECK(purged->text == R"("ada purged q3 with 2")");
    CHECK(trace == "aqf");
  }

  SECTION("the error is the supplier's, unchanged, and nothing runs after it") {
    const Session session{.user = "bob", .trace = &trace};
    const auto refused = tools[0].invoke(session, json(R"({"name":"q3"})"));
    REQUIRE_FALSE(refused);
    CHECK(refused.error().message == "caller bob is no administrator");
    CHECK(refused.error().model_message == "only an administrator may do this");
    CHECK(refused.error().issues.empty());
    CHECK(trace == "a");
  }

  SECTION("a later supplier refuses after an earlier one supplied") {
    const Session session{.user = "ada", .quota = 0, .trace = &trace};
    const auto refused = tools[0].invoke(session, json(R"({"name":"q3"})"));
    REQUIRE_FALSE(refused);
    CHECK(refused.error().model_message == "the quota is used up");
    CHECK(trace == "aq");
  }

  SECTION("arguments that do not decode fail before any supplier runs") {
    const Session session{.user = "ada", .trace = &trace};
    const auto invalid = tools[0].invoke(session, json(R"({"name":3})"));
    REQUIRE_FALSE(invalid);
    CHECK(invalid.error().model_message == "$.name must be a string");
    CHECK(trace.empty());
  }

  SECTION("a tool that returns nothing, and one that returns a Result") {
    const Session ada{.user = "ada", .trace = &trace};
    const auto locked = tools[1].invoke(ada, json("{}"));
    REQUIRE(locked);
    CHECK(locked->text == "{}");
    CHECK(trace == "af");

    const Session bob{.user = "bob"};
    const auto refused = tools[1].invoke(bob, json("{}"));
    REQUIRE_FALSE(refused);
    CHECK(refused.error().model_message == "only an administrator may do this");

    const auto halved = tools[2].invoke(ada, json(R"({"count":8})"));
    REQUIRE(halved);
    CHECK(halved->text == "4");
    const auto odd = tools[2].invoke(ada, json(R"({"count":7})"));
    REQUIRE_FALSE(odd);
    CHECK(odd.error().model_message == "count is odd");
    const auto denied = tools[2].invoke(bob, json(R"({"count":8})"));
    REQUIRE_FALSE(denied);
    CHECK(denied.error().model_message == "only an administrator may do this");
  }

  SECTION("a reference that the host does not have") {
    const auto deposit = scrydantic::bind<^^passing_tools::deposit, Session>();
    const auto refused = deposit[0].invoke(Session{}, json(R"({"coins":5})"));
    REQUIRE_FALSE(refused);
    CHECK(refused.error().model_message == "there is no vault");
  }
}

TEST_CASE("try_apply() returns a supplier's refusal") {
  std::string trace{};
  const Session ada{.user = "ada", .quota = 2, .trace = &trace};
  const Session bob{.user = "bob", .trace = &trace};

  const auto purged = scrydantic::try_apply<^^guarded_tools::purge, Session>(
      ada, decoded<^^guarded_tools::purge, Session>(R"({"name":"q3"})"));
  REQUIRE(purged);
  CHECK(*purged == "ada purged q3 with 2");
  CHECK(trace == "aqf");

  trace.clear();
  const auto refused = scrydantic::try_apply<^^guarded_tools::purge, Session>(
      bob, decoded<^^guarded_tools::purge, Session>(R"({"name":"q3"})"));
  REQUIRE_FALSE(refused);
  CHECK(refused.error().message == "caller bob is no administrator");
  CHECK(refused.error().model_message == "only an administrator may do this");
  CHECK(trace == "a");

  // A function that returns nothing gives a Status.
  CHECK(scrydantic::try_apply<^^guarded_tools::lock, Session>(ada, {}));
  CHECK_FALSE(scrydantic::try_apply<^^guarded_tools::lock, Session>(bob, {}));

  // A function that returns a Result keeps its failure apart from a refusal.
  const auto halved = scrydantic::try_apply<^^guarded_tools::halve, Session>(
      ada, decoded<^^guarded_tools::halve, Session>(R"({"count":8})"));
  REQUIRE(halved);
  CHECK(*halved == 4);
  const auto odd = scrydantic::try_apply<^^guarded_tools::halve, Session>(
      ada, decoded<^^guarded_tools::halve, Session>(R"({"count":7})"));
  REQUIRE(odd);
  REQUIRE_FALSE(*odd);
  CHECK(odd->error().model_message == "count is odd");
  const auto denied = scrydantic::try_apply<^^guarded_tools::halve, Session>(
      bob, decoded<^^guarded_tools::halve, Session>(R"({"count":8})"));
  REQUIRE_FALSE(denied);
  CHECK(denied.error().model_message == "only an administrator may do this");

  // A reference comes back as a reference_wrapper.
  Vault vault{.coins = 3};
  const Session rich{.vault = &vault};
  const auto opened = scrydantic::try_apply<^^handed::opened, Session>(rich, {});
  REQUIRE(opened);
  CHECK(&opened->get() == &vault);
  CHECK_FALSE(scrydantic::try_apply<^^handed::opened, Session>(Session{}, {}));

  // A function that no supplier can refuse is called the same way.
  const Database database{7};
  const Session session{.database = &database};
  const auto label = scrydantic::try_apply<^^placed_tools::between, Session>(
      session,
      decoded<^^placed_tools::between, Session>(R"({"name":"nut","copies":3})"));
  REQUIRE(label);
  CHECK(*label == "nutx3/7");
}

TEST_CASE("a toolbox member function takes supplied parameters") {
  const Database database{7};
  Journal journal{};
  const Session session{
      .user = "ada", .database = &database, .now = 5, .journal = &journal};

  const auto archive = std::make_shared<Archive>();
  const auto tools = scrydantic::bind<Session>(archive);
  REQUIRE(tools.size() == 3);
  CHECK(tools[0].input_schema.text ==
        R"({"additionalProperties":false,"properties":{"key":{"type":"string"}},)"
        R"("required":["key"],"type":"object"})");
  constexpr auto described = scrydantic::describe<Archive, Session>();
  for (std::size_t index = 0; index < tools.size(); ++index) {
    CHECK(tools[index].input_schema.text == described[index].input_schema);
  }

  const auto fetched = tools[0].invoke(session, json(R"({"key":"k1"})"));
  REQUIRE(fetched);
  CHECK(fetched->text == R"("rec:k1/7")");

  const auto noted = tools[1].invoke(session, json(R"({"line":"seen"})"));
  REQUIRE(noted);
  CHECK(noted->text == "1");
  CHECK(journal.lines == std::vector<std::string>{"seen"});

  // After an explicit object parameter and the context.
  const auto counted = tools[2].invoke(session, json("{}"));
  REQUIRE(counted);
  CHECK(counted->text == "9");
}

TEST_CASE("apply() and try_apply() call a member function on an object") {
  const Database database{7};
  const Session ada{.user = "ada", .database = &database};
  const Session bob{.user = "bob", .database = &database};
  Shelf shelf{};

  CHECK(scrydantic::apply<^^Shelf::fetch, Session>(
            shelf, ada, decoded<^^Shelf::fetch, Session>(R"({"key":"k2"})")) ==
        "shelf:k2/7");

  const auto stocked = scrydantic::try_apply<^^Shelf::stock, Session>(
      shelf, ada, decoded<^^Shelf::stock, Session>(R"({"count":4})"));
  REQUIRE(stocked);
  CHECK(*stocked == 4);
  const auto refused = scrydantic::try_apply<^^Shelf::stock, Session>(
      shelf, bob, decoded<^^Shelf::stock, Session>(R"({"count":4})"));
  REQUIRE_FALSE(refused);
  CHECK(refused.error().model_message == "only an administrator may do this");
}

TEST_CASE("the guide's call context examples say what the code does") {
  const Database database{42};
  const Desk desk{.user = "ada", .database = &database};

  const auto tools = scrydantic::bind<^^desk_tools, Desk>();
  REQUIRE(tools.size() == 2);
  CHECK(tools[0].input_schema.text ==
        R"({"additionalProperties":false,"properties":{"name":{"type":"string"}},)"
        R"("required":["name"],"type":"object"})");
  const auto exported = tools[0].invoke(desk, json(R"({"name":"q3"})"));
  REQUIRE(exported);
  CHECK(exported->text ==
        scrydantic::encode(desk_tools::export_report("q3", *desk.database, desk.stop))
            ->text);

  const auto refused = tools[1].invoke(Desk{.user = "bob"}, json(R"({"name":"q3"})"));
  REQUIRE_FALSE(refused);
  CHECK(refused.error().model_message == "only an administrator may do this");
  CHECK(desk_tools::deleted.empty());

  // The guide's section for library authors: try_apply() returns the refusal.
  using Arguments = scrydantic::arguments_t<^^desk_tools::delete_report, Desk>;
  auto arguments = scrydantic::decode<Arguments>(R"({"name":"q3"})");
  REQUIRE(arguments);
  const auto kept = scrydantic::try_apply<^^desk_tools::delete_report, Desk>(
      Desk{.user = "bob"}, Arguments{*arguments});
  REQUIRE_FALSE(kept);
  CHECK(kept.error().model_message == "only an administrator may do this");
  CHECK(desk_tools::deleted.empty());
  const auto deleted = scrydantic::try_apply<^^desk_tools::delete_report, Desk>(
      desk, std::move(*arguments));
  CHECK(deleted);
  CHECK(desk_tools::deleted == std::vector<std::string>{"ada:q3"});
}

TEST_CASE("a binding without a context supplies nothing") {
  // The parameter that a Session supplies is an argument here, which the model
  // gives.
  const auto plain = scrydantic::bind<^^clock_tools>();
  REQUIRE(plain.size() == 1);
  CHECK(plain[0].input_schema.text ==
        scrydantic::input_schema_v<scrydantic::arguments_t<^^clock_tools::timed>>);
  const auto timed = plain[0].invoke(json(R"({"offset":1,"clock":{"now":9}})"));
  REQUIRE(timed);
  CHECK(timed->text == "10");
  const auto missing = plain[0].invoke(json(R"({"offset":1})"));
  REQUIRE_FALSE(missing);
  CHECK(missing.error().model_message == "$.clock is a required member");
  CHECK(scrydantic::apply<^^clock_tools::timed>(
            decoded<^^clock_tools::timed>(R"({"offset":1,"clock":{"now":9}})")) == 10);

  // So it is with a context that has no supplier for the type.
  const auto guest = scrydantic::bind<^^clock_tools, Guest>();
  const auto guessed =
      guest[0].invoke(Guest{}, json(R"({"offset":1,"clock":{"now":9}})"));
  REQUIRE(guessed);
  CHECK(guessed->text == "10");

  // And with the context that supplies it, the host gives it.
  const auto supplied = scrydantic::bind<^^clock_tools, Session>();
  const auto hosted = supplied[0].invoke(Session{.now = 5}, json(R"({"offset":1})"));
  REQUIRE(hosted);
  CHECK(hosted->text == "6");
}
