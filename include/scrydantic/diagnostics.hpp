#pragma once

#if !defined(__cpp_impl_reflection)
#error "Scrydantic requires a compiler implementing P2996 (GCC 16 or newer)"
#endif

#include <meta>
#include <scrydantic/detail/meta.hpp>
#include <scrydantic/detail/tools.hpp>
#include <string_view>

/// The reason behind each of Scrydantic's compile-time checks, as text.
///
/// A concept such as Encodable says whether a type passes; the matching function
/// here says why it does not. Each one returns an empty view when the check
/// passes. Otherwise it returns the text that Scrydantic's own entry point
/// `static_assert`s with, which names the type, the member path, and the reason.
///
/// A library that wraps Scrydantic's entry points can then reject a type at its
/// own call site, and skip its body as Scrydantic does, so that a rejected type
/// gives one error and not a cascade:
///
/// ```cpp
/// template <typename T> Status save(const T& value) {
///   constexpr std::string_view problem = scrydantic::encodable_diagnostic<T>();
///   static_assert(problem.empty(), problem);
///   if constexpr (!problem.empty()) {
///     return {};
///   } else {
///     // ... scrydantic::encode(value) ...
///   }
/// }
/// ```
///
/// The text has static storage duration. Its wording can change in any release;
/// only whether it is empty is part of the API.
namespace scrydantic {

/// Why a type is not a SupportedValue.
/// @tparam Type Type to check.
/// @return Empty when Type satisfies SupportedValue, and otherwise the reason.
template <typename Type>
[[nodiscard]] consteval std::string_view supported_value_diagnostic() {
  return detail::supported_diagnostic<Type>();
}

/// Why a type is not Encodable, as encode() and encode_verbatim() report it.
/// @tparam Type Type to check.
/// @return Empty when Type satisfies Encodable, and otherwise the reason.
template <typename Type>
[[nodiscard]] consteval std::string_view encodable_diagnostic() {
  return detail::encodable_diagnostic<Type>();
}

/// Why a type is not Decodable, as decode() reports it.
/// @tparam Type Type to check.
/// @return Empty when Type satisfies Decodable, and otherwise the reason.
template <typename Type>
[[nodiscard]] consteval std::string_view decodable_diagnostic() {
  return detail::decodable_diagnostic<Type>();
}

/// Why a type is not ToolArguments, as input_schema_v and bind_handler() report
/// it.
/// @tparam Type Type to check.
/// @return Empty when Type satisfies ToolArguments, and otherwise the reason.
template <typename Type>
[[nodiscard]] consteval std::string_view tool_arguments_diagnostic() {
  return detail::tool_arguments_diagnostic<Type>();
}

/// Why a callable does not satisfy ToolHandlerFor<Handler, Args, Context>, as
/// bind_handler() reports it: the reason that `Args` is not ToolArguments, or else
/// the reason that the handler does not fit it.
/// @tparam Handler Callable to check, as bind_handler() would receive it.
/// @tparam Args Argument aggregate the handler receives.
/// @tparam Context Call context a handler may take first, or void for none.
/// @return Empty when the handler satisfies ToolHandlerFor, and otherwise the
/// reason.
template <typename Handler, typename Args, typename Context = void>
[[nodiscard]] consteval std::string_view tool_handler_diagnostic() {
  constexpr std::string_view arguments = detail::tool_arguments_diagnostic<Args>();
  if constexpr (!arguments.empty()) {
    return arguments;
  } else {
    return detail::tool_handler_diagnostic<Handler, Args, Context>();
  }
}

/// Why a class is not a Toolbox<Type, Context>, as bind(std::shared_ptr<Toolbox>)
/// reports it.
/// @tparam Type Toolbox class to check, `const` for a const toolbox.
/// @tparam Context Call context a tool member function may take first, or void.
/// @return Empty when Type satisfies Toolbox, and otherwise the reason.
template <typename Type, typename Context = void>
[[nodiscard]] consteval std::string_view toolbox_diagnostic() {
  return detail::toolbox_diagnostic<Type, Context>();
}

/// Why bind<Entity, Context>() cannot bind a function or a namespace.
///
/// The text starts with the call, such as `scrydantic::bind<^^forecast>(): `, and
/// then gives the reason. A wrapper passes the name of its own entry point, so the
/// call in the text is the one its user wrote.
/// @tparam Entity Reflection of a tool function, a static member function, or a
/// namespace.
/// @tparam Context Call context the tools may take first, or void for none.
/// @param api Name of the function the user called.
/// @return Empty when bind<Entity, Context>() binds, and otherwise the reason.
template <std::meta::info Entity, typename Context = void>
[[nodiscard]] consteval std::string_view
tools_diagnostic(const std::string_view api = "scrydantic::bind") {
  return detail::entity_tools_diagnostic<Entity, Context>(api);
}

} // namespace scrydantic
