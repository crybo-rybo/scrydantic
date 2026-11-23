#pragma once

#include <array>
#include <concepts>
#include <cstddef>
#include <functional>
#include <memory>
#include <meta>
#include <optional>
#include <scrydantic/bound_tool.hpp>
#include <scrydantic/codec.hpp>
#include <scrydantic/detail/decode.hpp>
#include <scrydantic/detail/encode.hpp>
#include <scrydantic/detail/json_string.hpp>
#include <scrydantic/detail/meta.hpp>
#include <scrydantic/detail/model.hpp>
#include <scrydantic/detail/tools.hpp>
#include <scrydantic/error.hpp>
#include <scrydantic/json.hpp>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

// Turns reflected tool declarations and typed handlers into bound tools: a
// generated schema plus a handler that decodes the arguments strictly, calls the
// C++ function, and encodes what it returns. The call itself, of any function
// with its decoded argument object, is what apply() publishes.
//
// This is the implementation of tools.hpp. It builds the public types that
// bound_tool.hpp declares: ToolFunction, BoundTool, and ToolDescriptor.
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
    return encode_value<Value>(*result);
  } else {
    return encode_value<ResultType>(result);
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
// input, into the ToolFunction of `Context`. The input is the parsed argument
// object: a ToolFunction parses the text of a call that passes text, so the body
// reads a view either way.
template <typename Context, typename Body>
[[nodiscard]] ToolFunction<Context> tool_function_of(Body body) {
  if constexpr (std::is_void_v<Context>) {
    return ToolFunction<Context>{
        [body = std::move(body)](const JsonView& input) -> Result<Json> {
          return body(no_context{}, input);
        }};
  } else {
    return ToolFunction<Context>{
        [body = std::move(body)](const Context& context,
                                 const JsonView& input) -> Result<Json> {
          return body(context, input);
        }};
  }
}

// bind_handler() has already checked `Args` and `Handler`, so neither is checked
// again here. The lambda's call operator is const, as a ToolFunction's is, so the
// stored callable is invoked as const, which is the call ToolHandlerFor checked.
template <typename Args, typename Context, typename Handler>
[[nodiscard]] ToolFunction<Context> make_tool_handler(Handler&& handler) {
  using Callable = std::decay_t<Handler>;
  return tool_function_of<Context>(
      [callable = Callable{std::forward<Handler>(handler)}](
          [[maybe_unused]] const context_argument_t<Context>& context,
          const JsonView& input) -> Result<Json> {
        auto arguments = detail::decode<Args>(input);
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
// `Args`, whose identity already carries the function's parameter names. Only
// the templates keyed on a bound_function or a bound_tool read facts that can
// differ between units.

// Calls a function, on `object` when it is a non-static member function.
template <std::meta::info Function, typename Object, typename... Values>
decltype(auto) call_on_object([[maybe_unused]] Object* object, Values&&... values) {
  if constexpr (std::meta::is_class_member(Function) &&
                !std::meta::is_static_member(Function)) {
    return object->[:Function:](std::forward<Values>(values)...);
  } else {
    return [:Function:](std::forward<Values>(values)...);
  }
}

// Calls a function with its value arguments, preceded by the context when it
// takes one. This is a function template rather than a generic lambda because
// GCC 16 instantiates both branches of an if constexpr on an enclosing template
// parameter inside a generic lambda.
template <std::meta::info Function, typename Context, typename Object,
          typename... Values>
decltype(auto)
call_function(Object* object,
              [[maybe_unused]] const context_argument_t<Context>& context,
              Values&&... values) {
  if constexpr (takes_context(Function, ^^Context)) {
    return call_on_object<Function>(object, context, std::forward<Values>(values)...);
  } else {
    return call_on_object<Function>(object, std::forward<Values>(values)...);
  }
}

// Spreads a decoded argument object over the function's parameter list: nothing,
// the whole object, or one synthesized member per parameter. It returns exactly
// what the function returns. This is the one call behind apply() and behind a
// bound tool's handler.
template <std::meta::info Function, typename Context, typename Object, typename Args>
decltype(auto) apply_function(Object* object,
                              const context_argument_t<Context>& context,
                              [[maybe_unused]] Args& arguments) {
  constexpr auto form = arguments_form_of(Function, ^^Context);
  if constexpr (form == arguments_form::none) {
    return call_function<Function, Context>(object, context);
  } else if constexpr (form == arguments_form::aggregate) {
    return call_function<Function, Context>(object, context, std::move(arguments));
  } else {
    static constexpr auto members = declared_members_v<Args>;
    return [&]<std::size_t... Index>(std::index_sequence<Index...>) -> decltype(auto) {
      return call_function<Function, Context>(
          object, context, std::move(arguments.[:members[Index]:])...);
    }(std::make_index_sequence<members.size()>{});
  }
}

// Why apply() cannot call a bound function without an object; empty when it
// can, and for an unbound one, which arguments_t has already reported.
template <typename Binding, typename Object>
consteval std::string_view object_diagnostic() {
  if constexpr (is_bound_v<Binding> && std::is_void_v<Object>) {
    const auto function = Binding::function;
    if (std::meta::is_class_member(function) &&
        !std::meta::is_static_member(function)) {
      return static_string(
          call_diagnostic("scrydantic::apply", function,
                          entity_name(function) +
                              " is a non-static member function; pass the object to "
                              "call it on, as apply(object, arguments)"));
    }
  }
  return {};
}

// What every apply() overload does. `object` is null, as a `void*`, for a call
// on no object, and `context` is a no_context for a binding without one. A
// binding that cannot be called is skipped, so it fails with its one diagnostic.
template <typename Binding, typename Context, typename Object, typename Args>
decltype(auto) apply_binding(Object* object, const context_argument_t<Context>& context,
                             Args& arguments) {
  constexpr std::string_view problem = object_diagnostic<Binding, Object>();
  static_assert(problem.empty(), problem);
  if constexpr (is_bound_v<Binding> && problem.empty()) {
    return apply_function<Binding::function, Context>(object, context, arguments);
  }
}

// The handler of one reflected function. `object` is the toolbox a member tool
// is bound to; the handler shares ownership of it, so the toolbox lives as long
// as any handler bound to it. The handler is called as const but reaches the
// toolbox through the pointer, so a tool member function that is not const still
// changes the toolbox: that state is the toolbox's own, not the handler's.
template <typename Tool, typename Context, typename Object>
[[nodiscard]] ToolFunction<Context>
make_function_handler(std::shared_ptr<Object> object) {
  using Binding = typename Tool::binding;
  using Args = function_arguments_t<Binding>;
  return tool_function_of<Context>(
      [object = std::move(object)](const context_argument_t<Context>& context,
                                   const JsonView& input) -> Result<Json> {
        auto arguments = detail::decode<Args>(input);
        if (!arguments) {
          return std::unexpected(std::move(arguments.error()));
        }
        return run_tool([&]() -> decltype(auto) {
          return apply_function<Binding::function, Context>(object.get(), context,
                                                            *arguments);
        });
      });
}

// The schema of what a successful call of a tool function returns: schema_v of
// the value it returns, alone or in a Result. A function that returns void or
// Status has none, and the view is empty: the `{}` such a tool answers with is
// an acknowledgment, not a value with a contract. The return type is part of
// the function's type, so this is keyed on the function alone.
template <std::meta::info Function> consteval std::string_view output_schema_of() {
  constexpr auto result = std::meta::return_type_of(Function);
  if constexpr (is_acknowledgment(result)) {
    return {};
  } else {
    return schema_v<typename[:plain_type(result_value_type(result)):]>;
  }
}

// The contract of one bound_tool, named and described as the binding unit saw
// the function.
template <typename Tool> [[nodiscard]] constexpr ToolDescriptor tool_descriptor() {
  return ToolDescriptor{
      .name = Tool::name,
      .description = Tool::description,
      .input_schema = input_schema_v<function_arguments_t<typename Tool::binding>>,
      .output_schema = output_schema_of<Tool::binding::function>(),
  };
}

template <typename Tool, typename Context, typename Object>
[[nodiscard]] BoundTool<Context> make_bound_tool(std::shared_ptr<Object> object) {
  static constexpr ToolDescriptor descriptor = tool_descriptor<Tool>();
  // No schema is empty text, so an empty view can only mean that there is none.
  return BoundTool<Context>{
      .name = std::string{descriptor.name},
      .description = std::string{descriptor.description},
      .input_schema = Json{.text = std::string{descriptor.input_schema}},
      .output_schema =
          descriptor.output_schema.empty()
              ? std::nullopt
              : std::optional{Json{.text = std::string{descriptor.output_schema}}},
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
