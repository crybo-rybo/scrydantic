// Annotate a few functions and a toolbox, publish them as an MCP-style
// `tools/list` result, and serve `tools/call` requests. The program needs no
// network or model, and exits 0 only when what it printed is what it expects.

#include <cmath>
#include <cstdint>
#include <expected>
#include <iostream>
#include <memory>
#include <optional>
#include <scrydantic/scrydantic.hpp>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

namespace unit_tools {

enum class Scale : std::uint8_t {
  celsius,
  fahrenheit,
};

struct Reading {
  [[= scrydantic::description{"Temperature in degrees"}]] double degrees{};
  Scale scale{Scale::celsius};
};

[[= scrydantic::tool{"Convert a temperature to the other scale"}]] inline Reading
    convert(Reading reading) {
  if (reading.scale == Scale::celsius) {
    return Reading{.degrees = reading.degrees * 9.0 / 5.0 + 32.0,
                   .scale = Scale::fahrenheit};
  }
  return Reading{.degrees = (reading.degrees - 32.0) * 5.0 / 9.0,
                 .scale = Scale::celsius};
}

[[= scrydantic::tool{"Round a number to the nearest whole number"}]] inline scrydantic::
    Result<std::int64_t>
        round_number(double value) {
  // 2^63 is the first double past the range. INT64_MAX converted to double rounds
  // up to it, so it cannot be the bound of an inclusive check.
  constexpr double limit = 0x1p63;
  const double rounded = std::round(value);
  if (rounded < -limit || rounded >= limit) {
    return std::unexpected(
        scrydantic::tool_error("the rounded value does not fit in 64 bits"));
  }
  return static_cast<std::int64_t>(rounded);
}

} // namespace unit_tools

// A toolbox: its annotated member functions become tools bound to one object.
class Notebook {
public:
  [[
    = scrydantic::tool{"Write a note"}, = scrydantic::name{"write_note"}
  ]] scrydantic::Status write(std::string text) {
    if (text.empty()) {
      return std::unexpected(scrydantic::tool_error("a note needs text"));
    }
    notes_.push_back(std::move(text));
    return {};
  }

  [[= scrydantic::tool{"Count the notes written so far"}]] std::size_t
      count_notes() const {
    return notes_.size();
  }

private:
  std::vector<std::string> notes_{};
};

namespace {

// The MCP shapes, declared as reflected values so Scrydantic writes and reads
// them. Keys that are not C++ identifiers in MCP's spelling get a name annotation.
struct ListedTool {
  std::string_view name;
  std::string_view description;
  [[= scrydantic::name{"inputSchema"}]] scrydantic::Json input_schema;
};

struct ToolList {
  std::vector<ListedTool> tools;
};

struct CallRequest {
  std::string name;
  scrydantic::Json arguments;
};

struct[[= scrydantic::tag{"text"}]] TextContent {
  std::string text;
};

struct CallResult {
  std::vector<std::variant<TextContent>> content;
  [[= scrydantic::name{"isError"}]] bool is_error{};
};

[[nodiscard]] CallResult call(std::vector<scrydantic::BoundTool<>>& tools,
                              const std::string_view request_text) {
  auto request = scrydantic::decode<CallRequest>(request_text);
  if (!request) {
    return {.content = {TextContent{request.error().model_message}}, .is_error = true};
  }
  for (auto& tool : tools) {
    if (tool.name == request->name) {
      auto result = tool.invoke(std::move(request->arguments));
      if (!result) {
        return {.content = {TextContent{result.error().model_message}},
                .is_error = true};
      }
      return {.content = {TextContent{std::move(result->text)}}};
    }
  }
  return {.content = {TextContent{"unknown tool " + request->name}}, .is_error = true};
}

// tools/list: each tool's name, description, and input schema.
[[nodiscard]] scrydantic::Result<scrydantic::Json>
list(const std::vector<scrydantic::BoundTool<>>& tools) {
  ToolList listing{};
  for (const auto& tool : tools) {
    listing.tools.push_back(ListedTool{
        .name = tool.name,
        .description = tool.description,
        .input_schema = tool.input_schema,
    });
  }
  return scrydantic::encode(listing);
}

// The tool names a tools/list result lists, in order, borrowed from `listing`.
[[nodiscard]] std::vector<std::string_view>
listed_names(const scrydantic::JsonView& listing) {
  std::vector<std::string_view> names{};
  const auto listed = listing.find("tools");
  for (std::size_t index = 0; listed && index < listed->size(); ++index) {
    names.push_back(listed->at(index)->find("name")->string().value_or(""));
  }
  return names;
}

// One tools/call request and the result a client expects for it.
struct Exchange {
  std::string_view request{};
  std::string_view expected{};
};

// What a client would check, after the four tools in declaration order: a decoded
// and converted reading, a schema-derived refusal the model can act on, a rounded
// number, and a value the schema allows but the tool refuses to round.
constexpr Exchange exchanges[] = {
    {R"({"name":"convert","arguments":{"degrees":100,"scale":"celsius"}})",
     R"({"content":[{"text":"{\"degrees\":212,\"scale\":\"fahrenheit\"}",)"
     R"("type":"text"}],"isError":false})"},
    {R"({"name":"write_note","arguments":{}})",
     R"({"content":[{"text":"$.text is a required member","type":"text"}],)"
     R"("isError":true})"},
    {R"({"name":"round_number","arguments":{"value":2.5}})",
     R"({"content":[{"text":"3","type":"text"}],"isError":false})"},
    {R"({"name":"round_number","arguments":{"value":1e100}})",
     R"({"content":[{"text":"the rounded value does not fit in 64 bits",)"
     R"("type":"text"}],"isError":true})"},
};

} // namespace

int main() {
  auto tools = scrydantic::bind<^^unit_tools>();
  for (auto& tool : scrydantic::bind(std::make_shared<Notebook>())) {
    tools.push_back(std::move(tool));
  }

  const auto listed = list(tools);
  if (!listed) {
    std::cerr << listed.error().message << '\n';
    return 1;
  }
  std::cout << "tools/list -> " << listed->text << '\n';
  const auto listing = scrydantic::JsonView::parse(*listed);
  bool ok = listing && listed_names(*listing) ==
                           std::vector<std::string_view>{"convert", "round_number",
                                                         "write_note", "count_notes"};

  for (const auto& exchange : exchanges) {
    const auto result = scrydantic::encode(call(tools, exchange.request));
    if (!result) {
      std::cerr << result.error().message << '\n';
      return 1;
    }
    std::cout << "tools/call -> " << result->text << '\n';
    ok = ok && result->text == exchange.expected;
  }
  return ok ? 0 : 1;
}
