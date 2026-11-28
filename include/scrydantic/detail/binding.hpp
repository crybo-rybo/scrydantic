#pragma once

#include <algorithm>
#include <array>
#include <concepts>
#include <cstddef>
#include <functional>
#include <memory>
#include <meta>
#include <optional>
#include <scrydantic/bound_tool.hpp>
#include <scrydantic/codec.hpp>
#include <scrydantic/decode_options.hpp>
#include <scrydantic/detail/constraints.hpp>
#include <scrydantic/detail/decode.hpp>
#include <scrydantic/detail/encode.hpp>
#include <scrydantic/detail/failure.hpp>
#include <scrydantic/detail/json_string.hpp>
#include <scrydantic/detail/meta.hpp>
#include <scrydantic/detail/model.hpp>
#include <scrydantic/detail/tools.hpp>
#include <scrydantic/detail/validate.hpp>
#include <scrydantic/error.hpp>
#include <scrydantic/json.hpp>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

// Turns reflected tool declarations and typed handlers into bound tools: a
// generated schema plus a handler that decodes the arguments strictly, calls the
// C++ function, and checks and encodes what it returns. The call itself, of any
// function with its decoded argument object, is what apply() publishes.
//
// This is the implementation of tools.hpp. It builds the public types that
// bound_tool.hpp declares: ToolFunction, BoundTool, and ToolDescriptor.
namespace scrydantic::detail {

// Checks the value a tool returned against the constraints and validators of
// its own type, as validate() checks a value. The tool's output schema states
// those constraints to whoever reads it, and encoding checks none of them, so
// without this a tool could answer with what its schema forbids. A value that
// fails is the tool's fault and the host's to hear of: invalid_result_error().
template <typename Value> [[nodiscard]] Status check_result(const Value& value) {
  decode_report report{};
  if (!validate_value(value, decode_cursor{report}, 0)) {
    return std::unexpected(invalid_result_error(report));
  }
  return {};
}

// Every handler checks its result before it encodes it, with no way to turn
// that off, since it costs nothing where there is nothing to check: for a result
// type with no constraint and no validator anywhere in it, which is most of
// them, the check is not generated, and the handler is the code it was without
// one. For any other type the walk is a small part of the encode that follows.
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
    if constexpr (has_checks_v<Value>) {
      if (auto valid = check_result<Value>(*result); !valid) {
        return std::unexpected(std::move(valid.error()));
      }
    }
    return encode_value<Value>(*result);
  } else {
    if constexpr (has_checks_v<ResultType>) {
      if (auto valid = check_result<ResultType>(result); !valid) {
        return std::unexpected(std::move(valid.error()));
      }
    }
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
[[nodiscard]] ToolFunction<Context>
make_tool_handler(Handler&& handler, const DecodeOptions& options = {}) {
  using Callable = std::decay_t<Handler>;
  return tool_function_of<Context>(
      [callable = Callable{std::forward<Handler>(handler)},
       options]([[maybe_unused]] const context_argument_t<Context>& context,
                const JsonView& input) -> Result<Json> {
        auto arguments = detail::decode<Args>(input, options);
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
// `Args`, whose identity already carries the function's parameter names, their
// annotations, and which of them have a default argument. Only the templates
// keyed on a bound_function or a bound_tool read facts that can differ between
// units. A call with fewer values than the function has parameters is made only
// for an `Args` that marks the rest as defaulted, so only by a unit that sees
// those default arguments. Which parameters the context supplies is asked of
// the supplier specializations here as it was when the function was bound,
// and C++ requires the answers to agree (bound_function in tools.hpp).

// What the context supplies for the parameter at `Position`: what the
// parameter's supplier returns for the context, as it returns it, or, for the
// leading `const Context&` that no supplier fills, the context itself.
template <std::meta::info Function, typename Context, std::size_t Position>
decltype(auto) supply_parameter(const Context& context) {
  constexpr auto type =
      std::meta::type_of(std::meta::parameters_of(Function)[Position]);
  if constexpr (has_supplier(type, ^^Context)) {
    return supplier<typename[:plain_type(type):], Context>::supply(context);
  } else {
    return (context);
  }
}

// Fills in the parameters of a function that the context supplies, one after
// another in declaration order, and then calls `use` with them all, which makes
// the call and returns a `Return`. Each is held here until `use` has returned,
// and is passed on as the reference its supplier returned, or as an rvalue of
// the value it returned, or of the value in the Result it returned.
//
// A supplier that returns a failed Result ends this there, with its Error as
// the `Return`, unchanged: no later supplier runs, and neither does `use`, so
// the function is not called. That needs a `Return` that an Error converts to,
// which is why apply(), whose `Return` is the function's own, takes no supplier
// that may refuse (refusal_problem in tools.hpp).
//
// A function with nothing to supply comes straight to the call of `use`.
template <std::meta::info Function, typename Context, typename Return,
          std::size_t Next = 0, typename Use, typename... Supplied>
Return supply_then([[maybe_unused]] const context_argument_t<Context>& context,
                   Use&& use, Supplied&&... supplied) {
  if constexpr (Next == supplied_positions_of(Function, ^^Context).size()) {
    return std::forward<Use>(use)(std::forward<Supplied>(supplied)...);
  } else {
    decltype(auto) value =
        supply_parameter<Function, Context,
                         supplied_positions_of(Function, ^^Context)[Next]>(context);
    using Value = decltype(value);
    if constexpr (is_result_type(^^Value)) {
      if (!value) {
        return std::unexpected(std::forward<Value>(value).error());
      }
      return supply_then<Function, Context, Return, Next + 1>(
          context, std::forward<Use>(use), std::forward<Supplied>(supplied)...,
          *std::forward<Value>(value));
    } else {
      return supply_then<Function, Context, Return, Next + 1>(
          context, std::forward<Use>(use), std::forward<Supplied>(supplied)...,
          std::forward<Value>(value));
    }
  }
}

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

// How many members of a synthesized argument object every call passes: those
// before its first defaulted argument. A function's default arguments are its
// last parameters', so every member from there on is one.
consteval std::size_t required_argument_count(const std::meta::info arguments) {
  const auto members = data_members_of(plain_type(arguments));
  return static_cast<std::size_t>(std::ranges::find_if(members, is_defaulted_argument) -
                                  members.begin());
}

// What a synthesized member holds for its parameter, to move into the call: the
// member, or the value in a defaulted argument's std::optional, which the caller
// has found engaged.
template <std::meta::info Member, typename Args>
[[nodiscard]] constexpr auto& held_argument(Args& arguments) noexcept {
  if constexpr (is_defaulted_argument(Member)) {
    return *arguments.[:Member:];
  } else {
    return arguments.[:Member:];
  }
}

// What a call passes for the parameter at `Position`: the supplied value in its
// turn, as supply_then passes it on, or what the argument object holds for the
// parameter, moved into the call, which is the whole object or the synthesized
// member in its turn. This is a function template rather than a generic lambda
// because GCC 16 instantiates both branches of an if constexpr on an enclosing
// template parameter inside a generic lambda.
template <std::meta::info Function, typename Context, std::size_t Position,
          typename Args, typename... Supplied>
[[nodiscard]] constexpr decltype(auto)
call_argument([[maybe_unused]] Args& arguments,
              [[maybe_unused]] Supplied&&... supplied) noexcept {
  constexpr auto source = parameter_source_of(Function, ^^Context, Position);
  if constexpr (source.supplied) {
    return std::forward<Supplied...[source.ordinal]>(supplied...[source.ordinal]);
  } else if constexpr (arguments_form_of(Function, ^^Context) ==
                       arguments_form::aggregate) {
    return std::move(arguments);
  } else {
    return std::move(
        held_argument<declared_members_v<Args>[source.ordinal]>(arguments));
  }
}

// Calls a function with the first `Count` of its value arguments, which leaves
// each value parameter after them to its default argument, and with the values
// the context supplied, each parameter in its declared place.
template <std::meta::info Function, typename Context, std::size_t Count,
          typename Object, typename Args, typename... Supplied>
decltype(auto) call_with_leading(Object* object, [[maybe_unused]] Args& arguments,
                                 [[maybe_unused]] Supplied&&... supplied) {
  constexpr std::size_t first = object_parameter_count(Function);
  return [&]<std::size_t... Index>(std::index_sequence<Index...>) -> decltype(auto) {
    return call_on_object<Function>(
        object, call_argument<Function, Context, first + Index>(
                    arguments, std::forward<Supplied>(supplied)...)...);
  }(std::make_index_sequence<passed_parameter_count(Function, ^^Context, Count)>{});
}

// Calls a function with as many members of its synthesized argument object as
// were given: the first `Count`, which the caller has found given, and each
// defaulted argument after them until one that was left out. A call cannot
// pass an argument after one it leaves out, so the members after that one are
// not passed; decoding admits no argument object that gives one of them
// (leaves_out_the_rest in decode.hpp). Each count is a call of its own, since
// the default arguments are the function's and no value here.
template <std::meta::info Function, typename Context, std::size_t Count,
          typename Object, typename Args, typename... Supplied>
decltype(auto) call_with_given(Object* object, Args& arguments,
                               Supplied&&... supplied) {
  static constexpr auto members = declared_members_v<Args>;
  if constexpr (Count < members.size()) {
    if (arguments.[:members[Count]:].has_value()) {
      return call_with_given<Function, Context, Count + 1>(
          object, arguments, std::forward<Supplied>(supplied)...);
    }
  }
  return call_with_leading<Function, Context, Count>(
      object, arguments, std::forward<Supplied>(supplied)...);
}

// Spreads a decoded argument object over the function's parameter list: nothing,
// the whole object, or one synthesized member per parameter, with `supplied`,
// the values supply_then filled in, in the places of the parameters the context
// supplies. It returns exactly what the function returns. This is the one call
// behind apply(), try_apply(), and a bound tool's handler. A function without a
// default argument is called once, with every member, as call_with_given comes
// to at that count.
template <std::meta::info Function, typename Context, typename Object, typename Args,
          typename... Supplied>
decltype(auto) apply_function(Object* object, Args& arguments, Supplied&&... supplied) {
  constexpr auto form = arguments_form_of(Function, ^^Context);
  if constexpr (form == arguments_form::none) {
    return call_with_leading<Function, Context, 0>(object, arguments,
                                                   std::forward<Supplied>(supplied)...);
  } else if constexpr (form == arguments_form::aggregate) {
    return call_with_leading<Function, Context, 1>(object, arguments,
                                                   std::forward<Supplied>(supplied)...);
  } else {
    return call_with_given<Function, Context, required_argument_count(^^Args)>(
        object, arguments, std::forward<Supplied>(supplied)...);
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

// Why apply() cannot call a bound function for which a supplier may refuse;
// empty when none may, and for an unbound function.
template <typename Binding> consteval std::string_view refusal_diagnostic() {
  if constexpr (is_bound_v<Binding>) {
    const auto function = Binding::function;
    const auto reason = refusal_problem(function, Binding::context);
    if (!reason.empty()) {
      return static_string(call_diagnostic("scrydantic::apply", function,
                                           entity_name(function) + " " + reason));
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
  constexpr std::string_view refusal = refusal_diagnostic<Binding>();
  static_assert(refusal.empty(), refusal);
  if constexpr (is_bound_v<Binding> && problem.empty() && refusal.empty()) {
    using Return = typename[:std::meta::return_type_of(Binding::function):];
    return supply_then<Binding::function, Context, Return>(
        context, [&](auto&&... supplied) -> Return {
          return apply_function<Binding::function, Context>(
              object, arguments, std::forward<decltype(supplied)>(supplied)...);
        });
  }
}

// What try_apply() holds of what a function returns, since a Result holds no
// reference: the reference_wrapper of an lvalue reference, and the object an
// rvalue reference refers to.
template <typename Return>
using held_return_t =
    std::conditional_t<std::is_lvalue_reference_v<Return>,
                       std::reference_wrapper<std::remove_reference_t<Return>>,
                       std::remove_reference_t<Return>>;

// Makes a call and holds what it returned in `Applied`, the Result that
// try_apply() returns. This is a function template rather than a generic
// lambda for the reason call_argument gives.
template <typename Applied, typename Call>
[[nodiscard]] Applied hold_result(Call&& call) {
  if constexpr (std::is_void_v<decltype(std::forward<Call>(call)())>) {
    std::forward<Call>(call)();
    return {};
  } else {
    return Applied{std::in_place, std::forward<Call>(call)()};
  }
}

// What every try_apply() overload does: apply_binding, with a Result to return
// a supplier's refusal in.
template <typename Binding, typename Context, typename Object, typename Args>
auto try_apply_binding(Object* object, const Context& context, Args& arguments) {
  constexpr std::string_view problem = object_diagnostic<Binding, Object>();
  static_assert(problem.empty(), problem);
  if constexpr (is_bound_v<Binding> && problem.empty()) {
    using Return = typename[:std::meta::return_type_of(Binding::function):];
    using Applied = Result<held_return_t<Return>>;
    return supply_then<Binding::function, Context, Applied>(
        context, [&](auto&&... supplied) -> Applied {
          return hold_result<Applied>([&]() -> decltype(auto) {
            return apply_function<Binding::function, Context>(
                object, arguments, std::forward<decltype(supplied)>(supplied)...);
          });
        });
  }
}

// The handler of one reflected function. `object` is the toolbox a member tool
// is bound to; the handler shares ownership of it, so the toolbox lives as long
// as any handler bound to it. The handler is called as const but reaches the
// toolbox through the pointer, so a tool member function that is not const still
// changes the toolbox: that state is the toolbox's own, not the handler's.
//
// The arguments decode before anything is supplied, so a call that the model
// got wrong fails without a supplier having run, which may cost the host
// something or ask something of its user.
template <typename Tool, typename Context, typename Object>
[[nodiscard]] ToolFunction<Context>
make_function_handler(std::shared_ptr<Object> object,
                      const DecodeOptions& options = {}) {
  using Binding = typename Tool::binding;
  using Args = function_arguments_t<Binding>;
  return tool_function_of<Context>(
      [object = std::move(object), options](const context_argument_t<Context>& context,
                                            const JsonView& input) -> Result<Json> {
        auto arguments = detail::decode<Args>(input, options);
        if (!arguments) {
          return std::unexpected(std::move(arguments.error()));
        }
        return supply_then<Binding::function, Context, Result<Json>>(
            context, [&](auto&&... supplied) -> Result<Json> {
              return run_tool([&]() -> decltype(auto) {
                return apply_function<Binding::function, Context>(
                    object.get(), *arguments,
                    std::forward<decltype(supplied)>(supplied)...);
              });
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
[[nodiscard]] BoundTool<Context> make_bound_tool(std::shared_ptr<Object> object,
                                                 const DecodeOptions& options = {}) {
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
      .invoke = make_function_handler<Tool, Context>(std::move(object), options),
  };
}

// The tools one binding call binds, in order: free functions bound to no object
// (a null `object`), or a toolbox's member functions bound to it. Their code
// depends on nothing but the bound_tools named here, so every translation unit
// that sees the same tools instantiates the same code.
template <typename Context, typename... Tools, typename Object>
[[nodiscard]] std::vector<BoundTool<Context>>
bound_tools(tool_set<Tools...>, const std::shared_ptr<Object>& object,
            const DecodeOptions& options = {}) {
  std::vector<BoundTool<Context>> tools{};
  tools.reserve(sizeof...(Tools));
  (tools.push_back(make_bound_tool<Tools, Context>(object, options)), ...);
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
