#pragma once

#include <algorithm>
#include <array>
#include <concepts>
#include <cstddef>
#include <functional>
#include <memory>
#include <meta>
#include <scrydantic/codec.hpp>
#include <scrydantic/detail/decode.hpp>
#include <scrydantic/detail/encode.hpp>
#include <scrydantic/detail/json_string.hpp>
#include <scrydantic/detail/meta.hpp>
#include <scrydantic/detail/model.hpp>
#include <scrydantic/detail/tools.hpp>
#include <scrydantic/error.hpp>
#include <scrydantic/json.hpp>
#include <span>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

// Turns reflected tool declarations and typed handlers into bound tools: a
// generated schema plus a handler that decodes the arguments strictly, calls the
// C++ function, and encodes what it returns.
//
// This is the implementation of tools.hpp, and it builds the public types
// declared there: ToolFunction, BoundTool, and ToolDescriptor. tools.hpp declares
// them before it includes this header, so that each public type and its
// documentation are in the public header, and this header compiles only as part
// of tools.hpp.
namespace scrydantic::detail {

template <typename Return>
  requires(handler_result_problem<std::remove_cvref_t<Return>>({}).empty() &&
           !is_acknowledgment(^^Return))
[[nodiscard]] Result<Json> encode_handler_result(Return&& result) {
  using ResultType = std::remove_cvref_t<Return>;
  if constexpr (expected_traits<ResultType>::recognized) {
    // A Result<const T> is supported like a const T return; encode the T.
    using Value = std::remove_cv_t<typename expected_traits<ResultType>::value_type>;
    if (!result) {
      return std::unexpected(std::forward<Return>(result).error());
    }
    return encode_value<Value>(*result).and_then(validate_encoded_json);
  } else {
    return encode_value<ResultType>(result).and_then(validate_encoded_json);
  }
}

// What the model receives from a tool that returns void or a successful Status.
[[nodiscard]] inline Json acknowledgment() { return Json{.text = "{}"}; }

// Runs a tool body and turns whatever it returns into the tool result. The body
// is invoked exactly once, and its return value is encoded where it lands, with
// no extra copy or move.
template <typename Body> [[nodiscard]] Result<Json> run_tool(Body&& body) {
  using Return = decltype(std::forward<Body>(body)());
  if constexpr (std::is_void_v<Return>) {
    std::forward<Body>(body)();
    return acknowledgment();
  } else if constexpr (is_acknowledgment(^^Return)) {
    auto status = std::forward<Body>(body)();
    if (!status) {
      return std::unexpected(std::move(status).error());
    }
    return acknowledgment();
  } else {
    return encode_handler_result(std::forward<Body>(body)());
  }
}

// Stands in for the context of a binding that has none, so the generated code
// has one shape for both.
struct no_context {};

template <typename Context>
using context_argument_t =
    std::conditional_t<std::is_void_v<Context>, no_context, Context>;

// Wraps the shared body of a handler, which takes the context argument and the
// input, into the ToolFunction of `Context`.
template <typename Context, typename Body>
[[nodiscard]] ToolFunction<Context> tool_function_of(Body body) {
  if constexpr (std::is_void_v<Context>) {
    return ToolFunction<Context>{
        [body = std::move(body)](Json input) mutable -> Result<Json> {
          return body(no_context{}, std::move(input));
        }};
  } else {
    return ToolFunction<Context>{
        [body = std::move(body)](const Context& context,
                                 Json input) mutable -> Result<Json> {
          return body(context, std::move(input));
        }};
  }
}

// bind_handler() has already checked `Args` and `Handler`, so neither is checked
// again here.
template <typename Args, typename Context, typename Handler>
[[nodiscard]] ToolFunction<Context> make_tool_handler(Handler&& handler) {
  using Callable = std::decay_t<Handler>;
  return tool_function_of<Context>(
      [callable = Callable{std::forward<Handler>(handler)}](
          [[maybe_unused]] const context_argument_t<Context>& context,
          const Json& input) mutable -> Result<Json> {
        auto arguments = decode_arguments<Args>(input);
        if (!arguments) {
          return std::unexpected(std::move(arguments.error()));
        }
        return run_tool([&]() -> decltype(auto) {
          // The same preference ToolHandlerFor applies: the contextual form wins.
          if constexpr (takes_context_first<Callable, Context, Args>) {
            return std::invoke(callable, context, std::move(*arguments));
          } else {
            return std::invoke(callable, std::move(*arguments));
          }
        });
      });
}

// ---- Reflected functions -----------------------------------------------------

// The call helpers below are keyed on the function and the context alone, or on
// those and the argument object: their code depends only on the function's type
// and the context type, which are the same in every translation unit, and on
// `Args`, whose identity already carries the tool's parameter names. Only the
// templates keyed on a bound_tool read facts that can differ between units.

// Calls a tool function, on `object` when it is a non-static member function.
template <std::meta::info Function, typename Object, typename... Values>
decltype(auto) call_on_object([[maybe_unused]] Object* object, Values&&... values) {
  if constexpr (std::meta::is_class_member(Function) &&
                !std::meta::is_static_member(Function)) {
    return object->[:Function:](std::forward<Values>(values)...);
  } else {
    return [:Function:](std::forward<Values>(values)...);
  }
}

// Calls a tool function with its value arguments, preceded by the context when
// it takes one. This is a function template rather than a generic lambda
// because GCC 16 instantiates both branches of an if constexpr on an enclosing
// template parameter inside a generic lambda.
template <std::meta::info Function, typename Context, typename Object,
          typename... Values>
decltype(auto)
call_tool_function(Object* object,
                   [[maybe_unused]] const context_argument_t<Context>& context,
                   Values&&... values) {
  if constexpr (takes_context(Function, ^^Context)) {
    return call_on_object<Function>(object, context, std::forward<Values>(values)...);
  } else {
    return call_on_object<Function>(object, std::forward<Values>(values)...);
  }
}

// Spreads a decoded argument object over the function's parameter list: nothing,
// the whole object, or one synthesized member per parameter. `Args` is deduced
// rather than spelled tool_arguments_t<Tool>, because GCC 16 cannot mangle a
// function template whose signature contains a splice.
template <std::meta::info Function, typename Context, typename Object, typename Args>
decltype(auto) invoke_tool_function(Object* object,
                                    const context_argument_t<Context>& context,
                                    [[maybe_unused]] Args& arguments) {
  constexpr auto form = arguments_form_of(Function, ^^Context);
  if constexpr (form == arguments_form::none) {
    return call_tool_function<Function, Context>(object, context);
  } else if constexpr (form == arguments_form::aggregate) {
    return call_tool_function<Function, Context>(object, context, std::move(arguments));
  } else {
    static constexpr auto members = declared_members_v<Args>;
    return [&]<std::size_t... Index>(std::index_sequence<Index...>) -> decltype(auto) {
      return call_tool_function<Function, Context>(
          object, context, std::move(arguments.[:members[Index]:])...);
    }(std::make_index_sequence<members.size()>{});
  }
}

// The handler of one reflected function. `object` is the toolbox a member tool
// is bound to; the handler shares ownership of it, so the toolbox lives as long
// as any handler bound to it.
template <typename Tool, typename Context, typename Object>
[[nodiscard]] ToolFunction<Context>
make_function_handler(std::shared_ptr<Object> object) {
  using Args = tool_arguments_t<Tool>;
  return tool_function_of<Context>(
      [object = std::move(object)](const context_argument_t<Context>& context,
                                   const Json& input) -> Result<Json> {
        auto arguments = decode_arguments<Args>(input);
        if (!arguments) {
          return std::unexpected(std::move(arguments.error()));
        }
        return run_tool([&]() -> decltype(auto) {
          return invoke_tool_function<Tool::function, Context>(object.get(), context,
                                                               *arguments);
        });
      });
}

// The contract of one bound_tool, named and described as the binding unit saw
// the function.
template <typename Tool> [[nodiscard]] constexpr ToolDescriptor tool_descriptor() {
  return ToolDescriptor{
      .name = Tool::name,
      .description = Tool::description,
      .input_schema = input_schema_v<tool_arguments_t<Tool>>,
  };
}

template <typename Tool, typename Context, typename Object>
[[nodiscard]] BoundTool<Context> make_bound_tool(std::shared_ptr<Object> object) {
  static constexpr ToolDescriptor descriptor = tool_descriptor<Tool>();
  return BoundTool<Context>{
      .name = std::string{descriptor.name},
      .description = std::string{descriptor.description},
      .input_schema = Json{.text = std::string{descriptor.input_schema}},
      .invoke = make_function_handler<Tool, Context>(std::move(object)),
  };
}

// The tools one binding call binds, in order: free functions bound to no object
// (a null `object`), or a toolbox's member functions bound to it. Their code
// depends on nothing but the bound_tools named here, so every translation unit
// that sees the same tools instantiates the same code.
template <typename Context, typename... Tools, typename Object>
[[nodiscard]] std::vector<BoundTool<Context>>
bound_tools(tool_set<Tools...>, const std::shared_ptr<Object>& object) {
  std::vector<BoundTool<Context>> tools{};
  tools.reserve(sizeof...(Tools));
  (tools.push_back(make_bound_tool<Tools, Context>(object)), ...);
  return tools;
}

template <typename... Tools>
[[nodiscard]] constexpr std::array<ToolDescriptor, sizeof...(Tools)>
tool_descriptors(tool_set<Tools...>) {
  return {tool_descriptor<Tools>()...};
}

// ---- Dispatch by name --------------------------------------------------------

// Whether a tool name reads as one word in a list: ASCII letters, digits, `_`,
// `-`, and `.`, and at least one of them. That covers the names model APIs
// accept, and none of them holds the list's own `, `.
[[nodiscard]] constexpr bool is_plain_tool_name(const std::string_view name) noexcept {
  constexpr auto is_plain = [](const char character) {
    return (character >= 'a' && character <= 'z') ||
           (character >= 'A' && character <= 'Z') ||
           (character >= '0' && character <= '9') || character == '_' ||
           character == '-' || character == '.';
  };
  return !name.empty() && std::ranges::all_of(name, is_plain);
}

// Why ToolSet::invoke() found no tool. The requested name came from the model, so
// it is spelled as a JSON string literal, as decode echoes an unknown member's
// key: a name holding a quote or a control character still reads as one name.
// The known names are the ones the model was given, in the order it was given
// them. A plain one is written as it is, and any other as a JSON string literal
// too, so that a name holding `, ` cannot read as two.
template <typename Context>
[[nodiscard]] Error
unknown_tool_error(const std::string_view name,
                   const std::span<const BoundTool<Context>> tools) {
  std::string message{"unknown tool "};
  append_json_string(message, name);
  if (tools.empty()) {
    message += "; there are no tools";
  }
  std::string_view separator = "; known tools: ";
  for (const auto& tool : tools) {
    message += separator;
    if (is_plain_tool_name(tool.name)) {
      message += tool.name;
    } else {
      append_json_string(message, tool.name);
    }
    separator = ", ";
  }
  return tool_error(std::move(message));
}

// Why ToolSet::make() refused its tools. Either fault is the host's bug, which
// the model, not yet given the tools, cannot act on, so the error has no
// model_message.
[[nodiscard]] inline Error refused_tool_error(const std::string_view prefix,
                                              const std::string_view name,
                                              const std::string_view suffix = {}) {
  std::string message{prefix};
  append_json_string(message, name);
  message += suffix;
  return Error{.message = std::move(message)};
}

// Two tools of one name: a model that calls by name could never reach the
// second.
[[nodiscard]] inline Error duplicate_tool_error(const std::string_view name) {
  return refused_tool_error("two tools are named ", name);
}

// A tool whose handler is empty, as a default-constructed BoundTool's is.
// Calling it is undefined behavior, so the set refuses to hold it.
[[nodiscard]] inline Error missing_handler_error(const std::string_view name) {
  return refused_tool_error("tool ", name, " has no handler");
}

} // namespace scrydantic::detail
