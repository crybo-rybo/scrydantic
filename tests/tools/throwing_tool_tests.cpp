// Tools that throw: a handler does not catch, so what a tool throws reaches the
// caller of invoke() as it was thrown, from a function, a toolbox member, a
// typed handler, a validator of the arguments, and a call by name through a
// tool set.

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_exception.hpp>
#include <cstdint>
#include <memory>
#include <scrydantic/scrydantic.hpp>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace {

using Catch::Matchers::Message;

// Thrown by the tools below and by nothing else, so catching one shows that the
// exception arrived as its own type and not as a base class or an Error.
struct Jam : std::runtime_error {
  using std::runtime_error::runtime_error;
};

// Not a std::exception: a handler has no base class to catch it by.
struct Fault {
  std::int32_t code{};
};

// A host's call context. Scrydantic only passes it through.
struct Operator {
  std::string name{};
};

[[nodiscard]] scrydantic::Json json(std::string text) {
  return scrydantic::Json{.text = std::move(text)};
}

} // namespace

namespace press_tools {

[[= scrydantic::tool{"Print some sheets"}]] inline std::int32_t
    print(std::int32_t sheets) {
  if (sheets > 3) {
    throw Jam{"jammed at sheet 4"};
  }
  return sheets;
}

[[= scrydantic::tool{"Cut a sheet"}]] inline scrydantic::Status
    cut(std::int32_t sheet) {
  if (sheet < 0) {
    throw Fault{.code = sheet};
  }
  return {};
}

} // namespace press_tools

namespace staffed_tools {

[[= scrydantic::tool{"Sign the run off"}]] inline std::string sign(const Operator& by,
                                                                   bool clean) {
  if (!clean) {
    throw Jam{by.name + " will not sign a spoiled run"};
  }
  return by.name;
}

} // namespace staffed_tools

namespace {

// A toolbox whose tool throws after changing it.
class Tray {
public:
  [[= scrydantic::tool{"Take sheets from the tray"}]] std::int32_t
      take(std::int32_t sheets) {
    if (sheets > left_) {
      left_ = 0;
      throw Jam{"the tray ran out"};
    }
    left_ -= sheets;
    return left_;
  }

  [[nodiscard]] std::int32_t left() const noexcept { return left_; }

private:
  std::int32_t left_{5};
};

// A validator that breaks the rule that it reports by returning.
inline scrydantic::Status stocked(const std::string& colour) {
  if (colour == "gold") {
    throw Jam{"no gold ink was ever stocked"};
  }
  return {};
}

struct Ink {
  [[= scrydantic::check<&stocked>]] std::string colour;
};

// The argument object of a typed handler.
struct Sheets {
  std::int32_t sheets{};
};

} // namespace

namespace ink_tools {

[[= scrydantic::tool{"Load an ink"}]] inline std::string load(Ink ink) {
  return ink.colour;
}

} // namespace ink_tools

TEST_CASE("an exception from a function tool leaves its handler as thrown") {
  const auto tools = scrydantic::bind<^^press_tools>();
  REQUIRE(tools.size() == 2);

  CHECK_THROWS_MATCHES(tools[0].invoke(json(R"({"sheets":4})")), Jam,
                       Message("jammed at sheet 4"));
  // The handler holds nothing a throw could leave behind: it serves the next call.
  CHECK(tools[0].invoke(json(R"({"sheets":3})")).value().text == "3");

  // Whatever is thrown, not only a std::exception.
  try {
    (void)tools[1].invoke(json(R"({"sheet":-7})"));
    FAIL("the tool's Fault did not reach the caller");
  } catch (const Fault& fault) {
    CHECK(fault.code == -7);
  }
}

TEST_CASE("an exception from a toolbox member leaves its handler as thrown") {
  const auto tray = std::make_shared<Tray>();
  const auto tools = scrydantic::bind(tray);
  REQUIRE(tools.size() == 1);

  CHECK(tools[0].invoke(json(R"({"sheets":2})")).value().text == "3");
  CHECK_THROWS_MATCHES(tools[0].invoke(json(R"({"sheets":4})")), Jam,
                       Message("the tray ran out"));
  // What the tool did before it threw stays done, and the toolbox stays bound.
  CHECK(tray->left() == 0);
  CHECK(tools[0].invoke(json(R"({"sheets":0})")).value().text == "0");
}

TEST_CASE("an exception from a typed handler's callable leaves it as thrown") {
  const auto fold = scrydantic::bind_handler<Sheets>([](Sheets given) {
    if (given.sheets > 3) {
      throw Jam{"too thick to fold"};
    }
    return given.sheets;
  });

  CHECK_THROWS_MATCHES(fold(json(R"({"sheets":4})")), Jam,
                       Message("too thick to fold"));
  CHECK(fold(json(R"({"sheets":1})")).value().text == "1");
}

TEST_CASE("an exception from a validator of the arguments leaves the handler") {
  const auto tools = scrydantic::bind<^^ink_tools>();
  REQUIRE(tools.size() == 1);

  CHECK_THROWS_MATCHES(tools[0].invoke(json(R"({"colour":"gold"})")), Jam,
                       Message("no gold ink was ever stocked"));
  CHECK(tools[0].invoke(json(R"({"colour":"black"})")).value().text == R"("black")");
}

TEST_CASE("a tool set's invoke lets a tool's exception through as thrown") {
  auto bound = scrydantic::bind<^^press_tools>();
  const auto tray = std::make_shared<Tray>();
  for (auto& tool : scrydantic::bind(tray)) {
    bound.push_back(std::move(tool));
  }
  const auto set = scrydantic::ToolSet<>::make(std::move(bound));
  REQUIRE(set);

  CHECK_THROWS_MATCHES(set->invoke("print", json(R"({"sheets":4})")), Jam,
                       Message("jammed at sheet 4"));
  CHECK_THROWS_MATCHES(set->invoke("take", json(R"({"sheets":9})")), Jam,
                       Message("the tray ran out"));
  CHECK_THROWS_AS(set->invoke("cut", json(R"({"sheet":-1})")), Fault);

  // The set is as it was: the same tools answer, and a returned error is still
  // returned.
  CHECK(set->invoke("print", json(R"({"sheets":1})")).value().text == "1");
  const auto unknown = set->invoke("staple", json("{}"));
  REQUIRE_FALSE(unknown);
  CHECK(unknown.error().model_message ==
        R"(unknown tool "staple"; known tools: print, cut, take)");
}

TEST_CASE("a tool set with a context lets a tool's exception through as thrown") {
  const auto set = scrydantic::ToolSet<Operator>::make(
      scrydantic::bind<^^staffed_tools, Operator>());
  REQUIRE(set);
  const Operator ada{.name = "ada"};

  CHECK_THROWS_MATCHES(set->invoke("sign", ada, json(R"({"clean":false})")), Jam,
                       Message("ada will not sign a spoiled run"));
  CHECK(set->invoke("sign", ada, json(R"({"clean":true})")).value().text == R"("ada")");
}
