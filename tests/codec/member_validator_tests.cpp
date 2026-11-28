// scrydantic::fail() and in-class [[= scrydantic::validator]] member functions:
// const, static, and explicit-object validators across decode(), validate(),
// flattened members, variants, and bound tool results.

#include <catch2/catch_test_macros.hpp>
#include <cstdint>
#include <memory>
#include <scrydantic/scrydantic.hpp>
#include <string>
#include <variant>
#include <vector>

namespace {

using scrydantic::fail;
using scrydantic::Status;

struct Interval {
  std::int32_t low{};
  std::int32_t high{};

  [[= scrydantic::validator]] Status ordered() const noexcept {
    return low <= high ? Status{}
                       : fail("must have low at most high",
                              "interval inverted: " + std::to_string(low) + " > " +
                                  std::to_string(high));
  }
};

struct StaticChecked {
  std::int32_t a{};
  std::int32_t b{};

  [[= scrydantic::validator]] static Status sum_positive(const StaticChecked& self) {
    if (self.a + self.b <= 0) {
      return fail("must have a positive sum");
    }
    return {};
  }
};

struct ExplicitChecked {
  std::int32_t width{};
  std::int32_t height{};

  [[= scrydantic::validator]] Status area_bounded(this const ExplicitChecked& self) {
    if (self.width * self.height > 100) {
      return fail("area must be at most 100");
    }
    return {};
  }
};

struct[[= scrydantic::tag{"interval"}]] TaggedInterval {
  [[= scrydantic::flatten]] Interval span;
};

struct[[= scrydantic::tag{"point"}]] TaggedPoint {
  std::int32_t x{};
};

using Mark = std::variant<TaggedInterval, TaggedPoint>;

struct OrderTracker {
  std::vector<std::string> log{};
};

OrderTracker tracker{};

struct Chained;
Status head_check(const Chained& value);

struct[[= scrydantic::check<&head_check>]] Chained {
  std::int32_t n{};

  [[= scrydantic::validator]] Status first_member() const {
    tracker.log.emplace_back("first");
    return n >= 10 ? Status{} : fail("must be at least 10");
  }

  [[= scrydantic::validator]] Status second_member() const {
    tracker.log.emplace_back("second");
    return n % 2 == 0 ? Status{} : fail("must be even");
  }
};

Status head_check(const Chained& value) {
  tracker.log.emplace_back("head");
  return value.n >= 0 ? Status{} : fail("must be non-negative");
}

struct Session {
  bool is_admin{};
};

struct Admin {};

} // namespace

template <> struct scrydantic::supplier<Admin, Session> {
  static scrydantic::Result<Admin> supply(const Session& session) {
    if (!session.is_admin) {
      return scrydantic::fail("admin required", "non-admin session rejected");
    }
    return Admin{};
  }
};

namespace fail_tools {

[[= scrydantic::tool{"Read an item"}]] inline scrydantic::Result<std::string>
    read_item(std::int32_t id) {
  if (id <= 0) {
    return scrydantic::fail("id must be positive",
                            "lookup skipped for non-positive id");
  }
  return "item-" + std::to_string(id);
}

[[= scrydantic::tool{"Produce an interval"}]] inline Interval
    make_interval(std::int32_t low, std::int32_t high) {
  return Interval{.low = low, .high = high};
}

[[= scrydantic::tool{"Admin action"}]] inline scrydantic::Status admin_only(Admin) {
  return {};
}

} // namespace fail_tools

namespace {

// Invalid validator declarations checked via diagnostics.
struct DuplicateValidator {
  std::int32_t x{};
  [[ = scrydantic::validator, = scrydantic::validator ]] Status ok() const {
    return {};
  }
};

struct DeletedValidator {
  std::int32_t x{};
  [[= scrydantic::validator]] Status ok() const = delete;
};

struct BadStaticValidator {
  std::int32_t x{};
  [[= scrydantic::validator]] static Status ok(int) { return {}; }
};

struct BadExplicitValidator {
  std::int32_t x{};
  [[= scrydantic::validator]] Status ok(this BadExplicitValidator&) { return {}; }
};

TEST_CASE("fail() converts directly to Status, Result<T>, and supplier results") {
  const auto tools = scrydantic::bind<^^fail_tools::read_item>();
  const auto bad = tools[0].invoke(R"({"id":0})");
  REQUIRE_FALSE(bad);
  CHECK(bad.error().issues.empty());
  CHECK(bad.error().model_message == "id must be positive");
  CHECK(bad.error().message == "lookup skipped for non-positive id");

  const auto good = tools[0].invoke(R"({"id":7})");
  REQUIRE(good);
  CHECK(good->text == R"("item-7")");

  const auto admin_tools = scrydantic::bind<^^fail_tools::admin_only, Session>();
  const auto denied = admin_tools[0].invoke(Session{.is_admin = false}, "{}");
  REQUIRE_FALSE(denied);
  CHECK(denied.error().model_message == "admin required");
  CHECK(denied.error().message == "non-admin session rejected");
}

TEST_CASE(
    "const, static, and explicit-object member validators run on decode and validate") {
  SECTION("const member function") {
    const auto ok = scrydantic::decode<Interval>(R"({"low":1,"high":5})");
    REQUIRE(ok);
    CHECK(ok->low == 1);
    CHECK(ok->high == 5);
    CHECK(scrydantic::validate(*ok).has_value());

    const auto bad = scrydantic::decode<Interval>(R"({"low":8,"high":2})");
    REQUIRE_FALSE(bad);
    REQUIRE(bad.error().issues.size() == 1);
    CHECK(bad.error().issues[0].kind == scrydantic::IssueKind::failed_check);
    CHECK(bad.error().model_message == "$ must have low at most high");
    CHECK(bad.error().message == "$ interval inverted: 8 > 2");

    const auto bad_val = scrydantic::validate(Interval{.low = 8, .high = 2});
    REQUIRE_FALSE(bad_val);
    CHECK(bad_val.error().model_message == "$ must have low at most high");
    CHECK(bad_val.error().message == "$ interval inverted: 8 > 2");
  }

  SECTION("static member function") {
    CHECK(scrydantic::decode<StaticChecked>(R"({"a":2,"b":3})").has_value());
    const auto bad = scrydantic::decode<StaticChecked>(R"({"a":-5,"b":2})");
    REQUIRE_FALSE(bad);
    CHECK(bad.error().model_message == "$ must have a positive sum");
    CHECK_FALSE(scrydantic::validate(StaticChecked{.a = -1, .b = 0}).has_value());
  }

  SECTION("explicit object member function") {
    CHECK(
        scrydantic::decode<ExplicitChecked>(R"({"width":10,"height":10})").has_value());
    const auto bad = scrydantic::decode<ExplicitChecked>(R"({"width":11,"height":10})");
    REQUIRE_FALSE(bad);
    CHECK(bad.error().model_message == "$ area must be at most 100");
    CHECK_FALSE(
        scrydantic::validate(ExplicitChecked{.width = 20, .height = 6}).has_value());
  }
}

TEST_CASE(
    "member validators run in order after class check<&fn> and stop at first failure") {
  tracker.log.clear();
  CHECK_FALSE(scrydantic::decode<Chained>(R"({"n":-1})").has_value());
  CHECK(tracker.log == std::vector<std::string>{"head"});

  tracker.log.clear();
  const auto low = scrydantic::decode<Chained>(R"({"n":4})");
  REQUIRE_FALSE(low);
  CHECK(low.error().model_message == "$ must be at least 10");
  CHECK(tracker.log == std::vector<std::string>{"head", "first"});

  tracker.log.clear();
  const auto odd = scrydantic::decode<Chained>(R"({"n":11})");
  REQUIRE_FALSE(odd);
  CHECK(odd.error().model_message == "$ must be even");
  CHECK(tracker.log == std::vector<std::string>{"head", "first", "second"});

  tracker.log.clear();
  CHECK(scrydantic::decode<Chained>(R"({"n":12})").has_value());
  CHECK(tracker.log == std::vector<std::string>{"head", "first", "second"});
}

TEST_CASE(
    "member validators run through flatten, variants, and tool result validation") {
  const auto bad_mark =
      scrydantic::decode<Mark>(R"({"type":"interval","low":9,"high":2})");
  REQUIRE_FALSE(bad_mark);
  CHECK(bad_mark.error().model_message == "$ must have low at most high");

  const auto good_mark =
      scrydantic::decode<Mark>(R"({"type":"interval","low":2,"high":9})");
  REQUIRE(good_mark);
  CHECK(scrydantic::validate(*good_mark).has_value());

  const auto tools = scrydantic::bind<^^fail_tools::make_interval>();
  const auto bad_result = tools[0].invoke(R"({"low":9,"high":2})");
  REQUIRE_FALSE(bad_result);
  CHECK(bad_result.error().issues.empty());
  CHECK(bad_result.error().model_message.empty());
  CHECK(bad_result.error().message == "result $ interval inverted: 9 > 2");
}

TEST_CASE("invalid member validator declarations report clear diagnostics") {
  CHECK(scrydantic::decodable_diagnostic<DuplicateValidator>().contains(
      "has more than one scrydantic::validator annotation"));
  CHECK(scrydantic::decodable_diagnostic<DeletedValidator>().contains("is deleted"));
  CHECK(scrydantic::decodable_diagnostic<BadStaticValidator>().contains(
      "and return scrydantic::Status"));
  CHECK(scrydantic::decodable_diagnostic<BadExplicitValidator>().contains(
      "must take only `this const"));
}

} // namespace
