// Tool sets: bound tools gathered under unique names, looked up and called by
// name, with the refusal a model receives for a name it was not given.

#include <catch2/catch_test_macros.hpp>
#include <cstdint>
#include <iterator>
#include <memory>
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

} // namespace

// Declared out of name order, so a list in set order differs from a sorted one.
namespace room_tools {

[[= scrydantic::tool{"Look around the room"}]] inline std::string look() {
  return "a quiet room";
}

[[= scrydantic::tool{"Walk some steps"}]] inline std::int32_t walk(std::int32_t steps) {
  return steps;
}

[[= scrydantic::tool{"Eat something"}]] inline scrydantic::Status
    eat(std::string food) {
  return std::unexpected(scrydantic::tool_error("not hungry", "refused " + food));
}

} // namespace room_tools

namespace guest_tools {

[[= scrydantic::tool{"Greet the session's user"}]] inline std::string
    greet(const Session& session, std::string greeting) {
  return greeting + ", " + session.user;
}

} // namespace guest_tools

namespace {

class Counter {
public:
  [[= scrydantic::tool{"Add to the counter"}]] std::int64_t bump(std::int64_t by) {
    total_ += by;
    return total_;
  }

  [[nodiscard]] std::int64_t total() const noexcept { return total_; }

private:
  std::int64_t total_{};
};

[[nodiscard]] scrydantic::Json json(std::string text) {
  return scrydantic::Json{.text = std::move(text)};
}

// The tools of two bindings, the first's then the second's.
template <typename Context>
[[nodiscard]] std::vector<scrydantic::BoundTool<Context>>
joined(std::vector<scrydantic::BoundTool<Context>> first,
       std::vector<scrydantic::BoundTool<Context>> second) {
  first.insert(first.end(), std::make_move_iterator(second.begin()),
               std::make_move_iterator(second.end()));
  return first;
}

[[nodiscard]] scrydantic::ToolSet<> room_set() {
  auto set = scrydantic::ToolSet<>::make(scrydantic::bind<^^room_tools>());
  REQUIRE(set);
  return std::move(*set);
}

template <typename Context>
[[nodiscard]] std::vector<std::string>
names_of(const scrydantic::ToolSet<Context>& set) {
  std::vector<std::string> names{};
  for (const auto& tool : set.tools()) {
    names.push_back(tool.name);
  }
  return names;
}

// Whether a `Set` lvalue can call a tool with `Arguments` after the name. A
// concept, because outside a template an invalid call in a requires-expression
// is an error rather than false.
template <typename Set, typename... Arguments>
concept invocable_by_name = requires(Set& set, Arguments&&... arguments) {
  set.invoke(std::string_view{}, std::forward<Arguments>(arguments)...);
};

} // namespace

// A set owns move-only handlers, and says so.
static_assert(!std::is_copy_constructible_v<scrydantic::ToolSet<>>);
static_assert(!std::is_copy_assignable_v<scrydantic::ToolSet<>>);
static_assert(std::is_nothrow_move_constructible_v<scrydantic::ToolSet<Session>>);
static_assert(std::is_nothrow_move_assignable_v<scrydantic::ToolSet<Session>>);
// A set calls its tools with exactly the context they take, and a const set
// only lends them out.
static_assert(invocable_by_name<scrydantic::ToolSet<>, scrydantic::Json>);
static_assert(
    !invocable_by_name<scrydantic::ToolSet<>, const Session&, scrydantic::Json>);
static_assert(
    invocable_by_name<scrydantic::ToolSet<Session>, const Session&, scrydantic::Json>);
static_assert(!invocable_by_name<scrydantic::ToolSet<Session>, scrydantic::Json>);
static_assert(!invocable_by_name<const scrydantic::ToolSet<>, scrydantic::Json>);

TEST_CASE("a tool set keeps its tools in the order it was given them") {
  auto set = scrydantic::ToolSet<>::make(joined(
      scrydantic::bind<^^room_tools>(), scrydantic::bind(std::make_shared<Counter>())));

  REQUIRE(set);
  CHECK(names_of(*set) == std::vector<std::string>{"look", "walk", "eat", "bump"});
  CHECK(set->tools()[3].description == "Add to the counter");
}

TEST_CASE("a tool set finds a tool by its exact name") {
  const auto set = room_set();

  const auto* eat = set.find("eat");
  REQUIRE(eat != nullptr);
  CHECK(eat->name == "eat");
  CHECK(eat->description == "Eat something");
  CHECK(eat == &set.tools()[2]);
  CHECK(set.find("look") == &set.tools()[0]);
  CHECK(set.find("fly") == nullptr);
  CHECK(set.find("ea") == nullptr);
  CHECK(set.find("eats") == nullptr);
  CHECK(set.find("") == nullptr);

  const scrydantic::ToolSet<> empty{};
  CHECK(empty.tools().empty());
  CHECK(empty.find("eat") == nullptr);
}

TEST_CASE("a tool set calls the named tool and returns what it returns") {
  auto set = room_set();

  const auto walked = set.invoke("walk", json(R"({"steps":3})"));
  REQUIRE(walked);
  CHECK(walked->text == "3");

  const auto looked = set.invoke("look", json("{}"));
  REQUIRE(looked);
  CHECK(looked->text == R"("a quiet room")");

  // The tool's own failures pass through unchanged: a refusal and a decode.
  const auto refused = set.invoke("eat", json(R"({"food":"soup"})"));
  REQUIRE_FALSE(refused);
  CHECK(refused.error().model_message == "not hungry");
  CHECK(refused.error().message == "refused soup");
  CHECK(refused.error().issues.empty());

  const auto undecoded = set.invoke("walk", json("{}"));
  REQUIRE_FALSE(undecoded);
  CHECK(undecoded.error().model_message == "$.steps is a required member");
  CHECK(undecoded.error().message == "$.steps is a required member");
  CHECK(undecoded.error().issues == std::vector<scrydantic::Issue>{{
                                        .kind = scrydantic::IssueKind::missing_member,
                                        .path = {std::string{"steps"}},
                                        .reason = "is a required member",
                                    }});
}

TEST_CASE("a tool set tells the model which tools there are") {
  auto set = room_set();

  // Set order, not name order.
  const auto unknown = set.invoke("fly", json("{}"));
  REQUIRE_FALSE(unknown);
  CHECK(unknown.error().model_message ==
        R"(unknown tool "fly"; known tools: look, walk, eat)");
  CHECK(unknown.error().message == unknown.error().model_message);
  CHECK(unknown.error().issues.empty());

  // The requested name is the model's own text, so it is escaped as one string.
  const auto escaped = set.invoke("say \"hi\"\n\x01", json("{}"));
  REQUIRE_FALSE(escaped);
  CHECK(escaped.error().model_message ==
        R"(unknown tool "say \"hi\"\n\u0001"; known tools: look, walk, eat)");

  scrydantic::ToolSet<> empty{};
  const auto none = empty.invoke("fly", json("{}"));
  REQUIRE_FALSE(none);
  CHECK(none.error().model_message == R"(unknown tool "fly"; there are no tools)");

  auto made_empty = scrydantic::ToolSet<>::make({});
  REQUIRE(made_empty);
  CHECK(made_empty->invoke("", json("{}")).error().model_message ==
        R"(unknown tool ""; there are no tools)");
}

TEST_CASE("a tool set refuses two tools of one name") {
  const auto set = scrydantic::ToolSet<>::make(
      joined(scrydantic::bind<^^room_tools>(), scrydantic::bind<^^room_tools>()));

  REQUIRE_FALSE(set);
  CHECK(set.error().message == R"(two tools are named "eat")");
  CHECK(set.error().model_message.empty());
  CHECK(set.error().issues.empty());
}

TEST_CASE("a tool set passes its call context through") {
  auto set =
      scrydantic::ToolSet<Session>::make(scrydantic::bind<^^guest_tools, Session>());
  REQUIRE(set);
  const Session session{.user = "ada"};

  const auto greeted = set->invoke("greet", session, json(R"({"greeting":"hello"})"));
  REQUIRE(greeted);
  CHECK(greeted->text == R"("hello, ada")");

  const auto unknown = set->invoke("wave", session, json("{}"));
  REQUIRE_FALSE(unknown);
  CHECK(unknown.error().model_message == R"(unknown tool "wave"; known tools: greet)");
}

TEST_CASE("a tool set calls a toolbox's tools on the one object") {
  auto counter = std::make_shared<Counter>();
  auto set = scrydantic::ToolSet<>::make(scrydantic::bind(counter));
  REQUIRE(set);

  const auto first = set->invoke("bump", json(R"({"by":2})"));
  const auto second = set->invoke("bump", json(R"({"by":3})"));
  REQUIRE(first);
  REQUIRE(second);
  CHECK(first->text == "2");
  CHECK(second->text == "5");
  CHECK(counter->total() == 5);
}

TEST_CASE("a moved tool set keeps its index") {
  auto source = room_set();
  auto moved = std::move(source);

  REQUIRE(moved.find("eat") != nullptr);
  CHECK(moved.find("eat")->description == "Eat something");
  const auto stepped = moved.invoke("walk", json(R"({"steps":1})"));
  REQUIRE(stepped);
  CHECK(stepped->text == "1");

  scrydantic::ToolSet<> assigned{};
  assigned = std::move(moved);
  CHECK(names_of(assigned) == std::vector<std::string>{"look", "walk", "eat"});
  const auto looked = assigned.invoke("look", json("{}"));
  REQUIRE(looked);
  CHECK(looked->text == R"("a quiet room")");
}
