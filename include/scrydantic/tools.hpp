#pragma once

#if !defined(__cpp_impl_reflection)
#error "Scrydantic requires a compiler implementing P2996 (GCC 16.2 or newer)"
#endif

#include <array>
#include <memory>
#include <meta>
#include <scrydantic/annotations.hpp>
#include <scrydantic/codec.hpp>
#include <scrydantic/detail/meta.hpp>
#include <scrydantic/detail/tools.hpp>
#include <scrydantic/error.hpp>
#include <scrydantic/json.hpp>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

/// Reflected tools: functions, namespaces, and toolbox classes annotated with
/// scrydantic::tool become tool contracts (name, description, generated input
/// schema) and handlers that decode their arguments strictly, call the C++
/// function, and encode what it returns.
///
/// A binding may give its tools a call context: a type of the host's choosing that
/// a tool function or handler receives by const reference as its leading
/// parameter, such as the identity of the call being serviced. `Context` names
/// it; `void`, the default, means the tools take none, and then a leading
/// parameter of any type is an argument the model supplies.
///
/// Tool discovery reads the declarations that precede the call in the calling
/// translation unit: a namespace is open, so the tools bound are the ones declared
/// in it before the call, and a function's name, description, and parameter names
/// are the ones its declarations so far give it. Two translation units that see
/// different declarations each bind what they see. The `Tools` template parameter
/// of each entry point records that view; leave it defaulted, or, in a library
/// that wraps these entry points, compute the same default on the wrapper with
/// tools_of() or toolbox_tools_of() and forward it.
namespace scrydantic {

/// Callables accepted for a particular reflected argument aggregate.
///
/// Handlers receive Args by value, optionally preceded by a `const Context&` when
/// `Context` is not void, and may return a supported value, a Result of one,
/// `void`, or Status.
/// @tparam Handler Callable to check.
/// @tparam Args Argument aggregate satisfying ToolArguments.
/// @tparam Context Call context a handler may take first, or void for none.
template <typename Handler, typename Args, typename Context = void>
concept ToolHandlerFor = ToolArguments<Args> &&
                         detail::tool_handler_problem<Handler, Args, Context>().empty();

/// Classes whose `[[= scrydantic::tool{...}]]` member functions bind() binds as
/// tools of one object.
///
/// A toolbox is a complete class that declares at least one tool member function
/// itself; inherited member functions are not considered. Every tool member must
/// be a public, non-deleted, non-`&&`-qualified function with a valid tool
/// signature, `const` when the toolbox is, and the tool names must be distinct.
/// A member with an explicit object parameter is instead called on the toolbox as
/// an lvalue, `const` when the toolbox is, which must initialize that parameter:
/// it may not be an rvalue reference, nor a non-const reference when the toolbox
/// is const, and a by-value parameter needs a toolbox it can copy.
/// Binding reports the first violation with the member's name and the reason.
/// @tparam Type Toolbox class, `const` for a const toolbox.
/// @tparam Context Call context a tool member function may take first, or void.
template <typename Type, typename Context = void>
concept Toolbox = detail::toolbox_problem(^^Type, ^^Context).empty();

/// Move-only type-erased handler of one bound tool.
///
/// It receives the canonical JSON argument object, preceded by a `const
/// Context&` when `Context` is not void, and returns the JSON result, encoded as
/// encode_verbatim() encodes it: valid JSON with keys in canonical order, and
/// spliced `Json` payloads byte for byte. A result that cannot be JSON, such as a
/// string holding invalid UTF-8, is a failure. A failure is an Error whose
/// `model_message` is the text meant for the model, empty when the failure carries
/// nothing the model could act on. Arguments that fail to decode also list their
/// issues in `issues`.
/// @tparam Context Call context passed to every invocation, or void for none.
template <typename Context = void>
using ToolFunction = typename detail::tool_function<Context>::type;

/// One tool ready to list and call: its contract and its handler.
/// @tparam Context Call context its handler receives, or void for none.
template <typename Context = void> struct BoundTool {
  /// Tool name the model sees.
  std::string name{};
  /// Tool description the model sees.
  std::string description{};
  /// Generated JSON Schema of the tool's argument object.
  Json input_schema{};
  /// Handler that decodes the arguments strictly, calls the C++ function, and
  /// encodes what it returns.
  ToolFunction<Context> invoke{};
};

/// One tool's contract without a handler, for hosts that only publish schemas.
///
/// Every view points at static storage, so a descriptor outlives any binding.
struct ToolDescriptor {
  /// Tool name the model sees.
  std::string_view name{};
  /// Tool description the model sees.
  std::string_view description{};
  /// Generated JSON Schema of the tool's argument object.
  std::string_view input_schema{};
};

/// Reflects the tools bind<^^Entity, Context>() binds: the tool functions a
/// namespace declares before the point of evaluation, or the one function.
///
/// This is the default of bind()'s `Tools` parameter. A library that wraps bind()
/// computes the same default on its own entry point and forwards it, so the tools
/// are the ones its caller's translation unit sees. An entity that cannot be bound
/// yields the empty set, and bind() reports why.
/// @param entity Reflection of a tool function, a static member function, or a
/// namespace.
/// @param context Reflection of the call context type, or of `void` for none.
/// @return Reflection of the tool set type.
consteval std::meta::info tools_of(const std::meta::info entity,
                                   const std::meta::info context) {
  if (!detail::entity_problem(entity, context).empty()) {
    return ^^detail::tool_set<>;
  }
  return detail::tool_set_of(std::meta::is_namespace(entity)
                                 ? detail::tool_functions_of(entity)
                                 : std::vector{entity},
                             context);
}

/// Reflects the tools bind<Context>(std::shared_ptr<Toolbox>) binds: the tool
/// member functions of the toolbox class, in declaration order.
///
/// This is the default of that bind()'s `Tools` parameter; see tools_of().
/// @param type Reflection of the toolbox type, `const` for a const toolbox.
/// @param context Reflection of the call context type, or of `void` for none.
/// @return Reflection of the tool set type.
consteval std::meta::info toolbox_tools_of(const std::meta::info type,
                                           const std::meta::info context) {
  if (!detail::toolbox_problem(type, context).empty()) {
    return ^^detail::tool_set<>;
  }
  return detail::tool_set_of(detail::tool_functions_of(detail::plain_type(type)),
                             context);
}

} // namespace scrydantic

// The entry points below call into detail/binding.hpp, which builds the types
// declared above, so it is included after them.
#include <scrydantic/detail/binding.hpp>

namespace scrydantic {

/// Binds a callable whose arguments are a reflected aggregate.
///
/// The returned handler decodes its JSON input strictly into `Args`, invokes the
/// callable with it, preceded by the context when the callable accepts one, and
/// encodes the typed return. The callable may return a supported value, a
/// Result of one, `void`, or Status; the last two answer `{}` on success. The
/// argument schema is input_schema_v<Args>.
///
/// The template is unconstrained so that a rejected type fails with a diagnostic
/// naming the offending member and the reason; ToolArguments and ToolHandlerFor
/// are the SFINAE-friendly form of the same checks.
/// @tparam Args Complete reflected argument aggregate satisfying ToolArguments.
/// @tparam Context Call context the handler may take first, or void for none.
/// @tparam Handler Move-constructible callable satisfying ToolHandlerFor<Handler,
/// Args, Context>.
/// @param handler Callable invoked with Args moved by value.
/// @return The type-erased handler.
template <typename Args, typename Context = void, typename Handler>
[[nodiscard]] ToolFunction<Context> bind_handler(Handler&& handler) {
  constexpr std::string_view arguments_problem =
      detail::tool_arguments_diagnostic<Args>();
  static_assert(arguments_problem.empty(), arguments_problem);
  if constexpr (!arguments_problem.empty()) {
    return {};
  } else {
    constexpr std::string_view handler_problem =
        detail::tool_handler_diagnostic<Handler, Args, Context>();
    static_assert(handler_problem.empty(), handler_problem);
    if constexpr (!handler_problem.empty()) {
      return {};
    } else {
      return detail::make_tool_handler<Args, Context>(std::forward<Handler>(handler));
    }
  }
}

/// Binds one annotated function, or every annotated function of a namespace.
///
/// `bind<^^forecast>()` binds a function declared with
/// `[[= scrydantic::tool{"description"}]]`; `bind<^^npc_tools>()` binds every
/// function so declared directly in that namespace, in declaration order. A tool
/// is named after the function unless a scrydantic::name annotation overrides it.
/// Its parameters, after an optional leading `const Context&`, are either nothing
/// (the tool takes `{}`), one plain aggregate (the argument object, as for
/// bind_handler()), or any other list of named parameters of supported types,
/// which is synthesized into an argument object with one required member per
/// parameter. It returns what a bind_handler() callable may return. A function
/// that breaks these rules fails to compile with its name and the reason.
/// @tparam Entity Reflection of a tool function, a static member function, or a
/// namespace.
/// @tparam Context Call context the tools may take first, or void for none.
/// @tparam Tools Leave defaulted: the tools `Entity` holds at the call site, each
/// with the name, description, and parameter names seen there.
/// @return The bound tools, in declaration order.
template <std::meta::info Entity, typename Context = void,
          typename Tools = typename[:tools_of(Entity, ^^Context):]>
[[nodiscard]] std::vector<BoundTool<Context>> bind() {
  constexpr std::string_view problem =
      detail::entity_tools_diagnostic<Entity, Context>("scrydantic::bind");
  static_assert(problem.empty(), problem);
  if constexpr (!problem.empty()) {
    return {};
  } else {
    return detail::bound_tools<Context>(Tools{}, std::shared_ptr<void>{});
  }
}

/// Binds a shared toolbox: every tool member function of `Toolbox`, bound to this
/// object.
///
/// The member functions follow the rules of bind<^^function>(). Each handler holds
/// a copy of the pointer, so the toolbox lives while any of them does. A `const`
/// toolbox admits only const member functions. A member function with an
/// explicit object parameter is called on the toolbox, which must initialize
/// that parameter as an lvalue, `const` when the toolbox is.
/// @tparam Context Call context the tools may take first, or void for none.
/// @tparam Toolbox Class satisfying Toolbox<Toolbox, Context>.
/// @tparam Tools Leave defaulted: the tool member functions of `Toolbox` as the
/// call site sees them, since an out-of-class definition can annotate a member
/// in some translation units only.
/// @param toolbox Toolbox to share. A null pointer binds no tools.
/// @return The bound tools, in declaration order, or none for a null toolbox.
template <typename Context = void, typename Toolbox,
          typename Tools = typename[:toolbox_tools_of(^^Toolbox, ^^Context):]>
[[nodiscard]] std::vector<BoundTool<Context>> bind(std::shared_ptr<Toolbox> toolbox) {
  constexpr std::string_view problem = detail::toolbox_diagnostic<Toolbox, Context>();
  static_assert(problem.empty(), problem);
  if constexpr (!problem.empty()) {
    return {};
  } else {
    if (toolbox == nullptr) {
      return {};
    }
    return detail::bound_tools<Context>(Tools{}, toolbox);
  }
}

/// Describes the tools bind<^^Entity, Context>() would bind, without handlers.
///
/// The descriptors are constant expressions, so a host can build a manifest at
/// compile time.
/// @tparam Entity Reflection of a tool function, a static member function, or a
/// namespace.
/// @tparam Context Call context the tools may take first, or void for none.
/// @tparam Tools Leave defaulted, as for bind().
/// @return One descriptor per tool, in declaration order.
template <std::meta::info Entity, typename Context = void,
          typename Tools = typename[:tools_of(Entity, ^^Context):]>
[[nodiscard]] constexpr auto describe() {
  constexpr std::string_view problem =
      detail::entity_tools_diagnostic<Entity, Context>("scrydantic::describe");
  static_assert(problem.empty(), problem);
  return detail::tool_descriptors(Tools{});
}

/// Describes the tools bind<Context>(std::shared_ptr<Toolbox>) would bind,
/// without a toolbox object or handlers.
/// @tparam Toolbox Class satisfying Toolbox<Toolbox, Context>.
/// @tparam Context Call context the tools may take first, or void for none.
/// @tparam Tools Leave defaulted, as for bind().
/// @return One descriptor per tool, in declaration order.
template <typename Toolbox, typename Context = void,
          typename Tools = typename[:toolbox_tools_of(^^Toolbox, ^^Context):]>
[[nodiscard]] constexpr auto describe() {
  constexpr std::string_view problem = detail::toolbox_diagnostic<Toolbox, Context>();
  static_assert(problem.empty(), problem);
  return detail::tool_descriptors(Tools{});
}

} // namespace scrydantic
