// A host for a few tools, with no network and no model. It binds annotated
// functions and a toolbox, lists what each tool takes and returns, calls them by
// name, and tells apart the ways a call can end. It exits 0 only when what it
// listed and what the calls answered are what it expects.

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <expected>
#include <iostream>
#include <memory>
#include <scrydantic/scrydantic.hpp>
#include <string>
#include <string_view>
#include <utility>
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

// A tool with a bug: the mean of no values is 0/0, which is not a number, and
// JSON cannot hold one. The host finds out when the result does not encode.
[[= scrydantic::tool{"Average some numbers"}]] inline double
    mean(std::vector<double> values) {
  double sum = 0.0;
  for (const double value : values) {
    sum += value;
  }
  return sum / static_cast<double>(values.size());
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

  // Throws std::out_of_range for a note that was never written.
  [[= scrydantic::tool{"Read the note at a position"}]] std::string
      read_note(std::size_t position) const {
    return notes_.at(position);
  }

private:
  std::vector<std::string> notes_{};
};

namespace {

// What a host publishes for each tool. One that returns no value has no output
// schema: its `{}` says only that the call succeeded.
void list(const scrydantic::ToolSet<>& tools) {
  for (const auto& tool : tools.tools()) {
    std::cout << tool.name << ": " << tool.description << '\n'
              << "  takes   " << tool.input_schema.text << '\n'
              << "  returns "
              << (tool.output_schema ? std::string_view{tool.output_schema->text}
                                     : "no value")
              << '\n';
  }
}

// How a call ended. Scrydantic returns the first four; the fifth it lets through.
enum class Outcome : std::uint8_t {
  returned,      // The tool's result.
  bad_arguments, // The arguments did not decode. The model can correct them.
  refused,       // The tool said no, or the set holds no tool of that name.
  failed,        // The result could not be encoded: a bug on the host's side.
  threw,         // The tool threw an exception.
};

// The host's answer to one call, with the text the model is given. It is a
// reflected value like any other, so Scrydantic writes it too.
struct Reply {
  Outcome outcome{};
  std::string text;
};

// Calls the tool the model named. The set is const: a call can change what a
// tool keeps, such as the notebook's notes, but not the set.
[[nodiscard]] Reply call(const scrydantic::ToolSet<>& tools,
                         const std::string_view name,
                         const std::string_view arguments) {
  scrydantic::Result<scrydantic::Json> result;
  try {
    result = tools.invoke(name, scrydantic::Json{.text = std::string{arguments}});
  } catch (const std::exception& thrown) {
    // A handler does not catch. The exception's text is for the log, and the
    // model gets a fixed message.
    std::clog << name << " threw: " << thrown.what() << '\n';
    return {.outcome = Outcome::threw, .text = "the tool failed"};
  }
  if (result) {
    return {.outcome = Outcome::returned, .text = std::move(result->text)};
  }
  const scrydantic::Error& error = result.error();
  if (!error.issues.empty()) {
    return {.outcome = Outcome::bad_arguments, .text = error.model_message};
  }
  if (!error.model_message.empty()) {
    return {.outcome = Outcome::refused, .text = error.model_message};
  }
  std::clog << name << " failed: " << error.message << '\n';
  return {.outcome = Outcome::failed, .text = "the tool failed"};
}

// What the listing should hold: each tool in binding order, and whether a call
// to it returns a value with a schema.
struct Listed {
  std::string_view name{};
  bool returns_value{};
};

constexpr Listed listed[] = {
    {.name = "convert", .returns_value = true},
    {.name = "mean", .returns_value = true},
    {.name = "write_note", .returns_value = false},
    {.name = "read_note", .returns_value = true},
};

// One call and the reply the host should give.
struct Exchange {
  std::string_view name{};
  std::string_view arguments{};
  std::string_view expected{};
};

// A decoded and converted reading, a schema-derived refusal the model can act
// on, the tool's own refusal, a note written and read back, a position past the
// last note, a mean of nothing, and a tool that does not exist, answered with
// the ones that do.
constexpr Exchange exchanges[] = {
    {"convert", R"({"degrees":100,"scale":"celsius"})",
     R"({"outcome":"returned","text":"{\"degrees\":212,\"scale\":\"fahrenheit\"}"})"},
    {"write_note", R"({})",
     R"({"outcome":"bad_arguments","text":"$.text is a required member"})"},
    {"write_note", R"({"text":""})",
     R"({"outcome":"refused","text":"a note needs text"})"},
    {"write_note", R"({"text":"water the plants"})",
     R"({"outcome":"returned","text":"{}"})"},
    {"read_note", R"({"position":0})",
     R"({"outcome":"returned","text":"\"water the plants\""})"},
    {"read_note", R"({"position":7})",
     R"({"outcome":"threw","text":"the tool failed"})"},
    {"mean", R"({"values":[]})", R"({"outcome":"failed","text":"the tool failed"})"},
    {"fly", R"({})",
     R"({"outcome":"refused","text":"unknown tool \"fly\"; known tools: convert, )"
     R"(mean, write_note, read_note"})"},
};

} // namespace

int main() {
  auto bound = scrydantic::bind<^^unit_tools>();
  for (auto& tool : scrydantic::bind(std::make_shared<Notebook>())) {
    bound.push_back(std::move(tool));
  }
  const auto tools = scrydantic::ToolSet<>::make(std::move(bound));
  if (!tools) {
    std::cerr << tools.error().message << '\n';
    return 1;
  }

  list(*tools);
  bool ok = std::ranges::equal(
      tools->tools(), listed,
      [](const scrydantic::BoundTool<>& tool, const Listed& expected) {
        return tool.name == expected.name &&
               tool.output_schema.has_value() == expected.returns_value;
      });

  for (const auto& exchange : exchanges) {
    const auto reply =
        scrydantic::encode(call(*tools, exchange.name, exchange.arguments));
    if (!reply) {
      std::cerr << reply.error().message << '\n';
      return 1;
    }
    std::cout << exchange.name << ' ' << exchange.arguments << " -> " << reply->text
              << '\n';
    ok = ok && reply->text == exchange.expected;
  }
  return ok ? 0 : 1;
}
