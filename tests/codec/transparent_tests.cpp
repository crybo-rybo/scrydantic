// Transparent single-member wrappers: schema generation, encoding, decoding,
// defaults, coercion, constraints, member and class validators, variants,
// nested transparency, and tool parameters/results.

#include <array>
#include <catch2/catch_test_macros.hpp>
#include <cstdint>
#include <map>
#include <memory>
#include <optional>
#include <scrydantic/scrydantic.hpp>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace {

using scrydantic::fail;
using scrydantic::Status;

Status has_at_sign(const std::string& text) {
  return text.contains('@') ? Status{} : fail("must contain '@'");
}

struct[[
  = scrydantic::transparent, = scrydantic::description{"TCP port"},
  = scrydantic::title{"Port"}
]] Port {
  [[= scrydantic::range{.min = 1, .max = 65535}]] std::uint16_t value{};
};

struct[[= scrydantic::transparent]] Email {
  [[
    = scrydantic::length{.min = 3, .max = 254}, = scrydantic::format{"email"},
    = scrydantic::check<&has_at_sign>
  ]] std::string value;
};

struct[[= scrydantic::transparent]] Slug {
  [[= scrydantic::length{.min = 1, .max = 32}]] std::string value;

  [[= scrydantic::validator]] Status no_spaces() const {
    return !value.contains(' ') ? Status{} : fail("must not contain spaces");
  }
};

struct[[ = scrydantic::transparent, = scrydantic::description{"HTTP port"} ]] HttpPort {
  Port port{};
};

struct[[= scrydantic::transparent]] TagList {
  [[= scrydantic::items{.min = 1, .max = 4}]] std::vector<Slug> values;
};

Status unprivileged(const Port& port) {
  return port.value >= 1024 ? Status{} : fail("must be an unprivileged port");
}

struct Config {
  std::string host;
  [[= scrydantic::description{"Listen port"}]] Port port{.value = 8080};
  [[= scrydantic::check<&unprivileged>]] Port admin_port{.value = 9090};
  std::optional<Port> metrics_port{};
  std::vector<Email> admins{};
  HttpPort http{.port = {.value = 80}};
  TagList tags{};
};

struct Server {
  std::string host;
  [[= scrydantic::description{"Listen port"}]] Port port{.value = 8080};
  [[= scrydantic::check<&unprivileged>]] Port admin_port{.value = 9090};
  std::optional<Port> metrics_port{};
  std::vector<Email> admins{};
  std::map<std::string, Port> extras{};
  HttpPort http{.port = {.value = 80}};
  TagList tags{};
};

struct[[= scrydantic::transparent]] MaybeText {
  std::optional<std::string> value;
};

struct NestedOptional {
  std::optional<MaybeText> bad;
};

struct[[= scrydantic::transparent]][[= scrydantic::transparent]] DoubleTransparent {
  int value{};
};

} // namespace

namespace port_tools {

[[= scrydantic::tool{"Listen on a port"}]] inline std::uint16_t listen(Port port) {
  return port.value;
}

[[= scrydantic::tool{"Return a port"}]] inline Port echo_port(std::uint16_t raw) {
  return Port{.value = raw};
}

} // namespace port_tools

namespace {

TEST_CASE("transparent wrappers encode, decode, validate, and emit schemas as their "
          "inner type") {
  CHECK(scrydantic::schema_v<Port> ==
        R"({"description":"TCP port","maximum":65535,"minimum":1,"title":"Port",)"
        R"("type":"integer"})");

  CHECK(scrydantic::schema_v<Email> ==
        R"({"format":"email","maxLength":254,"minLength":3,"type":"string"})");

  CHECK(scrydantic::schema_v<HttpPort> ==
        R"({"description":"HTTP port","maximum":65535,"minimum":1,"title":"Port",)"
        R"("type":"integer"})");

  const auto encoded = scrydantic::encode(Port{.value = 443});
  REQUIRE(encoded);
  CHECK(encoded->text == "443");

  const auto decoded = scrydantic::decode<Port>("443");
  REQUIRE(decoded);
  CHECK(decoded->value == 443);

  const auto out_of_range = scrydantic::decode<Port>("0");
  REQUIRE_FALSE(out_of_range);
  CHECK(out_of_range.error().model_message == "$ must be at least 1");

  const auto bad_type = scrydantic::decode<Port>(R"("443")");
  REQUIRE_FALSE(bad_type);
  CHECK(bad_type.error().model_message == "$ must be an integer");

  // Coercion works seamlessly through transparent wrappers.
  const auto coerced = scrydantic::decode<Port>(
      R"("443")", scrydantic::DecodeOptions{.coerce_scalars = true});
  REQUIRE(coerced);
  CHECK(coerced->value == 443);

  // Member and class validators both run at the wrapper's path.
  const auto bad_email = scrydantic::decode<Email>(R"("not-an-email")");
  REQUIRE_FALSE(bad_email);
  CHECK(bad_email.error().model_message == "$ must contain '@'");

  const auto bad_slug = scrydantic::decode<Slug>(R"("hello world")");
  REQUIRE_FALSE(bad_slug);
  CHECK(bad_slug.error().model_message == "$ must not contain spaces");

  CHECK_FALSE(scrydantic::validate(Port{.value = 0}).has_value());
  CHECK(scrydantic::validate(Port{.value = 8080}).has_value());
  CHECK_FALSE(scrydantic::validate(Slug{.value = "a b"}).has_value());
}

TEST_CASE(
    "transparent wrappers compose inside structs, containers, defaults, and checks") {
  CHECK(scrydantic::schema_v<Config> ==
        R"({"additionalProperties":false,"properties":{)"
        R"("admin_port":{"default":9090,"description":"TCP port","maximum":65535,)"
        R"("minimum":1,"title":"Port","type":"integer"},)"
        R"("admins":{"default":[],"items":{"format":"email","maxLength":254,)"
        R"("minLength":3,"type":"string"},"type":"array"},)"
        R"("host":{"type":"string"},)"
        R"("http":{"default":80,"description":"HTTP port","maximum":65535,)"
        R"("minimum":1,"title":"Port","type":"integer"},)"
        R"("metrics_port":{"anyOf":[{"description":"TCP port","maximum":65535,)"
        R"("minimum":1,"title":"Port","type":"integer"},{"type":"null"}],)"
        R"("default":null},)"
        R"("port":{"default":8080,"description":"Listen port","maximum":65535,)"
        R"("minimum":1,"title":"Port","type":"integer"},)"
        R"("tags":{"default":[],"items":{"maxLength":32,"minLength":1,)"
        R"("type":"string"},"maxItems":4,"minItems":1,"type":"array"}},)"
        R"("required":["host"],"type":"object"})");

  CHECK(scrydantic::schema_v<Server> ==
        R"({"additionalProperties":false,"properties":{)"
        R"("admin_port":{"description":"TCP port","maximum":65535,)"
        R"("minimum":1,"title":"Port","type":"integer"},)"
        R"("admins":{"items":{"format":"email","maxLength":254,)"
        R"("minLength":3,"type":"string"},"type":"array"},)"
        R"("extras":{"additionalProperties":{"description":"TCP port",)"
        R"("maximum":65535,"minimum":1,"title":"Port","type":"integer"},)"
        R"("type":"object"},)"
        R"("host":{"type":"string"},)"
        R"("http":{"description":"HTTP port","maximum":65535,)"
        R"("minimum":1,"title":"Port","type":"integer"},)"
        R"("metrics_port":{"anyOf":[{"description":"TCP port","maximum":65535,)"
        R"("minimum":1,"title":"Port","type":"integer"},{"type":"null"}]},)"
        R"("port":{"description":"Listen port","maximum":65535,)"
        R"("minimum":1,"title":"Port","type":"integer"},)"
        R"("tags":{"items":{"maxLength":32,"minLength":1,)"
        R"("type":"string"},"maxItems":4,"minItems":1,"type":"array"}},)"
        R"("required":["host"],"type":"object"})");

  const Server server{
      .host = "localhost",
      .port = {.value = 8080},
      .admin_port = {.value = 9090},
      .metrics_port = Port{.value = 9100},
      .admins = {Email{.value = "ada@example.com"}},
      .extras = {{"ssh", Port{.value = 22}}},
      .http = {.port = {.value = 80}},
      .tags = {.values = {Slug{.value = "prod"}}},
  };
  const auto encoded = scrydantic::encode(server);
  REQUIRE(encoded);
  CHECK(encoded->text == R"({"admin_port":9090,"admins":["ada@example.com"],)"
                         R"("extras":{"ssh":22},"host":"localhost","http":80,)"
                         R"("metrics_port":9100,"port":8080,"tags":["prod"]})");

  const auto round_trip = scrydantic::decode<Server>(encoded->text);
  REQUIRE(round_trip);
  CHECK(round_trip->port.value == 8080);
  CHECK(round_trip->http.port.value == 80);
  CHECK(round_trip->admins.size() == 1);
  CHECK(round_trip->admins[0].value == "ada@example.com");
  CHECK(round_trip->extras.at("ssh").value == 22);
  CHECK(scrydantic::validate(*round_trip).has_value());

  // Error paths point at the outer field, never at the inner member name.
  const auto invalid = scrydantic::decode<Server>(
      R"({"host":"localhost","port":0,"admin_port":80,"admins":["bad"],)"
      R"("extras":{"ssh":0},"http":0,"tags":["bad slug"]})");
  REQUIRE_FALSE(invalid);
  CHECK(invalid.error().model_message == "$.port must be at least 1\n"
                                         "$.admin_port must be an unprivileged port\n"
                                         "$.admins[0] must contain '@'\n"
                                         "$.extras.ssh must be at least 1\n"
                                         "$.http must be at least 1\n"
                                         "$.tags[0] must not contain spaces");
}

TEST_CASE("transparent wrappers participate in kind-dispatched variants") {
  using Endpoint = std::variant<Port, Email>;
  CHECK(scrydantic::schema_v<Endpoint> ==
        R"({"anyOf":[{"description":"TCP port","maximum":65535,"minimum":1,)"
        R"("title":"Port","type":"integer"},)"
        R"({"format":"email","maxLength":254,"minLength":3,"type":"string"}]})");

  const auto as_port = scrydantic::decode<Endpoint>("8080");
  REQUIRE(as_port);
  REQUIRE(std::holds_alternative<Port>(*as_port));
  CHECK(std::get<Port>(*as_port).value == 8080);

  const auto as_email = scrydantic::decode<Endpoint>(R"("ada@example.com")");
  REQUIRE(as_email);
  REQUIRE(std::holds_alternative<Email>(*as_email));
  CHECK(std::get<Email>(*as_email).value == "ada@example.com");

  // Integer + double dispatch unwraps transparent integer wrappers properly.
  using PortOrRatio = std::variant<Port, double>;
  const auto small = scrydantic::decode<PortOrRatio>("80");
  REQUIRE(small);
  REQUIRE(std::holds_alternative<Port>(*small));
  CHECK(std::get<Port>(*small).value == 80);

  const auto big = scrydantic::decode<PortOrRatio>("70000");
  REQUIRE(big);
  REQUIRE(std::holds_alternative<double>(*big));
  CHECK(std::get<double>(*big) == 70000.0);

  const auto frac = scrydantic::decode<PortOrRatio>("1.5");
  REQUIRE(frac);
  REQUIRE(std::holds_alternative<double>(*frac));
  CHECK(std::get<double>(*frac) == 1.5);
}

TEST_CASE("transparent wrappers work as lone tool parameters and validated results") {
  const auto listen_tools = scrydantic::bind<^^port_tools::listen>();
  REQUIRE(listen_tools.size() == 1);
  CHECK(listen_tools[0].input_schema.text ==
        R"({"additionalProperties":false,"properties":{)"
        R"("port":{"description":"TCP port","maximum":65535,"minimum":1,)"
        R"("title":"Port","type":"integer"}},"required":["port"],"type":"object"})");

  const auto ok = listen_tools[0].invoke(R"({"port":8080})");
  REQUIRE(ok);
  CHECK(ok->text == "8080");

  const auto bad = listen_tools[0].invoke(R"({"port":0})");
  REQUIRE_FALSE(bad);
  CHECK(bad.error().model_message == "$.port must be at least 1");

  const auto echo_tools = scrydantic::bind<^^port_tools::echo_port>();
  REQUIRE(echo_tools.size() == 1);
  REQUIRE(echo_tools[0].output_schema);
  CHECK(echo_tools[0].output_schema->text == scrydantic::schema_v<Port>);

  const auto valid_out = echo_tools[0].invoke(R"({"raw":443})");
  REQUIRE(valid_out);
  CHECK(valid_out->text == "443");

  // Returning Port{0} violates Port's constraint and fails as a host-side bug.
  const auto invalid_out = echo_tools[0].invoke(R"({"raw":0})");
  REQUIRE_FALSE(invalid_out);
  CHECK(invalid_out.error().issues.empty());
  CHECK(invalid_out.error().model_message.empty());
  CHECK(invalid_out.error().message == "result $ must be at least 1");
}

TEST_CASE("invalid transparent wrapper combinations are diagnosed") {
  CHECK(scrydantic::supported_value_diagnostic<DoubleTransparent>().contains(
      "has more than one scrydantic::transparent annotation"));
  CHECK(scrydantic::supported_value_diagnostic<NestedOptional>().contains(
      "nests std::optional directly"));
}

} // namespace
