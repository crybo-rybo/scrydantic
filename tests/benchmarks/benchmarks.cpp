// Throughput of the JSON core, the reflected codec, and bound tool calls on one
// representative document: a conversation of tagged content blocks, strings,
// whole and fractional numbers, enums, optionals, and opaque JSON payloads.
//
// Numbers are only meaningful from an optimized build:
//
//   cmake --preset release && cmake --build --preset release
//   build/release/tests/scrydantic_benchmarks
//
// Catch2's benchmark options apply, such as --benchmark-samples 200, and a
// test name or tag selects a subset: [json], [codec], or [tools].

#include <catch2/benchmark/catch_benchmark.hpp>
#include <catch2/catch_test_macros.hpp>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <scrydantic/detail/encode.hpp>
#include <scrydantic/detail/json_text.hpp>
#include <scrydantic/scrydantic.hpp>
#include <string>
#include <variant>
#include <vector>

namespace bench {

struct[[= scrydantic::tag{"text"}]] Text {
  std::string text{};
};

struct[[= scrydantic::tag{"tool_call"}]] ToolCall {
  scrydantic::Json arguments{};
  std::string id{};
  std::string name{};
};

struct[[= scrydantic::tag{"tool_result"}]] ToolResult {
  bool is_error{};
  scrydantic::Json result{};
  std::string tool_call_id{};
};

using Block = std::variant<Text, ToolCall, ToolResult>;

enum class Role : std::uint8_t {
  user,
  assistant,
};

struct Message {
  std::vector<Block> content{};
  Role role{};
  std::optional<double> temperature{};
  std::uint64_t tokens{};
};

struct Conversation {
  std::vector<Message> messages{};
  std::string system_prompt{};
  std::uint64_t version{};
};

struct Point {
  std::int32_t x{};
  std::int32_t y{};
};

struct Route {
  std::vector<Point> points{};
  std::string name{};
};

[[nodiscard]] Conversation make_conversation(const std::size_t count) {
  Conversation conversation{.system_prompt = "You are a careful assistant.",
                            .version = 3};
  for (std::size_t index = 0; index < count; ++index) {
    const auto id = "call_" + std::to_string(index);
    conversation.messages.push_back(Message{
        .content = {Text{.text = "Please look up the forecast for day " +
                                 std::to_string(index) + ", with \"quotes\"."},
                    ToolCall{.arguments = {.text = R"({"city":"Lisbon","days":3})"},
                             .id = id,
                             .name = "forecast"},
                    ToolResult{.result = {.text = R"({"high":21.5,"low":14})"},
                               .tool_call_id = id}},
        .role = index % 2 == 0 ? Role::user : Role::assistant,
        .temperature = 0.25 * static_cast<double>(index % 4),
        .tokens = 1000 + index,
    });
  }
  return conversation;
}

} // namespace bench

namespace bench_tools {

[[= scrydantic::tool{"Add two integers"}]] inline std::int64_t add(std::int64_t left,
                                                                   std::int64_t right) {
  return left + right;
}

[[= scrydantic::tool{"Reverse a route"}]] inline bench::Route
    reverse(bench::Route route) {
  return bench::Route{.points = {route.points.rbegin(), route.points.rend()},
                      .name = route.name};
}

} // namespace bench_tools

namespace {

// 100 messages of three blocks each: about 25 KB of canonical JSON.
constexpr std::size_t message_count = 100;

[[nodiscard]] const scrydantic::Json& conversation_text() {
  static const scrydantic::Json text =
      *scrydantic::encode(bench::make_conversation(message_count));
  return text;
}

} // namespace

TEST_CASE("JSON core", "[json]") {
  const auto& text = conversation_text();
  BENCHMARK("JsonView::parse") { return scrydantic::JsonView::parse(text); };
  BENCHMARK("validate_json") { return scrydantic::detail::validate_json(text.text); };
  BENCHMARK("JsonView::parse and to_json") {
    return scrydantic::JsonView::parse(text)->to_json();
  };
}

TEST_CASE("reflected codec", "[codec]") {
  const auto conversation = bench::make_conversation(message_count);
  const auto& text = conversation_text();
  BENCHMARK("encode (canonical)") { return scrydantic::encode(conversation); };
  BENCHMARK("encode_value (verbatim)") {
    return scrydantic::detail::encode_value(conversation);
  };
  BENCHMARK("decode from text") {
    return scrydantic::decode<bench::Conversation>(text);
  };
  const auto view = *scrydantic::JsonView::parse(text);
  BENCHMARK("decode from a parsed view") {
    return scrydantic::decode<bench::Conversation>(view);
  };
}

TEST_CASE("bound tool calls", "[tools]") {
  auto tools = scrydantic::bind<^^bench_tools>();
  REQUIRE(tools.size() == 2);
  auto& add = tools[0].invoke;
  auto& reverse = tools[1].invoke;
  BENCHMARK("synthesized arguments") {
    return add(scrydantic::Json{.text = R"({"left":40,"right":2})"});
  };
  BENCHMARK("aggregate arguments and result") {
    return reverse(scrydantic::Json{
        .text =
            R"({"name":"loop","points":[{"x":1,"y":2},{"x":3,"y":4},{"x":5,"y":6}]})"});
  };
}
