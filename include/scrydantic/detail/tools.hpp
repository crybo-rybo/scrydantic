#pragma once

#include <concepts>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <meta>
#include <scrydantic/annotations.hpp>
#include <scrydantic/detail/constraints.hpp>
#include <scrydantic/detail/meta.hpp>
#include <scrydantic/detail/model.hpp>
#include <scrydantic/error.hpp>
#include <scrydantic/json.hpp>
#include <string>
#include <string_view>
#include <type_traits>
#include <vector>

// The compile-time model of bound functions and reflected tools: how a parameter
// list becomes one argument object, which functions are tools, what they are
// named, and why a declaration cannot be bound. Everything here is a consteval
// function of a reflection; binding.hpp turns the answers into calls and
// handlers.
//
// A binding may give its functions a call context, a type of the host's choosing
// that a function receives by const reference as its leading parameter. The
// consteval functions take it as `context`, the reflection of that type, or of
// `void` when the binding has none.
namespace scrydantic::detail {

// ---- Annotated declarations --------------------------------------------------

// A class or namespace walk meets entities that cannot carry annotations, such
// as templates and namespace aliases, where annotations_of throws. None of those
// carries the annotation a walk looks for.
consteval std::vector<std::meta::info>
annotations_or_none(const std::meta::info entity) {
  try {
    return std::meta::annotations_of(entity);
  } catch (const std::meta::exception&) {
    return {};
  }
}

// Whether an entity a walk meets carries `what`: an annotation type, or an
// annotation class template, whose every specialization matches.
consteval bool is_annotated(const std::meta::info entity, const std::meta::info what) {
  for (const auto annotation : annotations_or_none(entity)) {
    if (annotation_matches(annotation, what)) {
      return true;
    }
  }
  return false;
}

// The functions a class or namespace declares that carry `what`, in declaration
// order. This is the walk behind annotated_functions_of() in tools.hpp, and the
// one that finds a scope's tools.
consteval std::vector<std::meta::info>
annotated_functions_of(const std::meta::info scope, const std::meta::info what) {
  std::vector<std::meta::info> functions{};
  for (const auto member :
       std::meta::members_of(scope, std::meta::access_context::unchecked())) {
    if (std::meta::is_function(member) && is_annotated(member, what)) {
      functions.push_back(member);
    }
  }
  return functions;
}

// The tool name the model sees: a name annotation, or else the identifier.
consteval std::string_view tool_name_of(const std::meta::info function) {
  if (const auto annotated = annotated_text(function, ^^name)) {
    return *annotated;
  }
  return std::meta::identifier_of(function);
}

consteval std::string_view tool_description_of(const std::meta::info function) {
  return *annotated_text(function, ^^tool);
}

// ---- Parameters ----------------------------------------------------------------

consteval bool has_context(const std::meta::info context) {
  return std::meta::dealias(context) != ^^void;
}

// The type a context parameter is declared with: `const Context&`.
consteval std::meta::info context_parameter_type(const std::meta::info context) {
  return std::meta::add_lvalue_reference(
      std::meta::add_const(std::meta::dealias(context)));
}

consteval bool is_context_parameter(const std::meta::info parameter,
                                    const std::meta::info context) {
  return has_context(context) && std::meta::dealias(std::meta::type_of(parameter)) ==
                                     context_parameter_type(context);
}

// A C++23 explicit object parameter (`this const Box& self`) is the object a
// member function is called on. Member-call syntax binds it to that object, so
// it is neither the context nor an argument the model supplies.
consteval bool has_explicit_object(const std::meta::info function) {
  const auto parameters = std::meta::parameters_of(function);
  return !parameters.empty() &&
         std::meta::is_explicit_object_parameter(parameters.front());
}

consteval std::size_t object_parameter_count(const std::meta::info function) {
  return has_explicit_object(function) ? 1U : 0U;
}

consteval bool takes_context(const std::meta::info function,
                             const std::meta::info context) {
  const auto parameters = std::meta::parameters_of(function);
  const auto first = object_parameter_count(function);
  return parameters.size() > first && is_context_parameter(parameters[first], context);
}

// The index of the first parameter the model supplies: after the explicit object
// parameter and the context, when the function has them.
consteval std::size_t first_value_index(const std::meta::info function,
                                        const std::meta::info context) {
  return object_parameter_count(function) +
         (takes_context(function, context) ? 1U : 0U);
}

// The parameters the model supplies, in declaration order.
consteval std::vector<std::meta::info>
value_parameters_of(const std::meta::info function, const std::meta::info context) {
  auto parameters = std::meta::parameters_of(function);
  parameters.erase(parameters.begin(),
                   parameters.begin() + static_cast<std::ptrdiff_t>(
                                            first_value_index(function, context)));
  return parameters;
}

// How a bound function receives its arguments. A lone parameter of plain aggregate
// type is the whole argument object, exactly as for bind_handler<Args>(); every other
// non-empty list is synthesized into an aggregate with one member per parameter.
enum class arguments_form : std::uint8_t {
  none,
  aggregate,
  synthesized,
};

consteval arguments_form arguments_form_of(const std::meta::info function,
                                           const std::meta::info context) {
  const auto values = value_parameters_of(function, context);
  if (values.empty()) {
    return arguments_form::none;
  }
  if (values.size() == 1 &&
      kind_of(std::meta::type_of(values.front())) == value_kind::aggregate) {
    return arguments_form::aggregate;
  }
  return arguments_form::synthesized;
}

// The argument object of a function that takes no arguments. Its schema is the
// empty closed object, and decoding accepts `{}` alone.
struct no_arguments {};

// ---- Binding identity ---------------------------------------------------------

// The member names of a synthesized argument object, each followed by a comma:
// the value parameters' identifiers. Empty for the other forms, whose generated
// code does not depend on parameter names.
consteval std::string synthesized_member_list(const std::meta::info function,
                                              const std::meta::info context) {
  std::string list{};
  if (arguments_form_of(function, context) == arguments_form::synthesized) {
    for (const auto parameter : value_parameters_of(function, context)) {
      list += owned_text(std::meta::identifier_of(parameter));
      list.push_back(',');
    }
  }
  return list;
}

// The text of a reflect_constant_string array, without its terminator.
template <std::meta::info Text>
inline constexpr std::string_view constant_text_v{[:Text:], sizeof([:Text:]) - 1U};

// One function as a binding sees it: the type binding_of() reflects, and the
// `Binding` that arguments_t and apply() carry.
//
// A function's parameter names come from the declarations of it that precede the
// point of evaluation, so two translation units can see one function
// differently. Each such fact is a template argument here, a constant string
// that GCC mangles by its content, so units that see different facts
// instantiate differently named code, which the linker cannot merge. The code
// generated for a binding reads these facts from here and never from the
// function's declarations, so what a specialization's name records is exactly
// what its code contains. Everything else it reads from the function, such as
// the parameter types, the return type, and the object parameter, is part of the
// function's type and the same in every unit. `Context` is the binding's call
// context, which decides whether the leading parameter is an argument.
//
// Another fact of the parameter list that a later declaration can change is one
// more template argument here, gathered by bound_function_of(). No entry point
// spells the arguments: each carries the binding whole.
template <std::meta::info Function, std::meta::info Context, std::meta::info Members>
struct bound_function {
  static constexpr std::meta::info function = Function;
  static constexpr std::meta::info context = Context;
  // The synthesized member names, as synthesized_member_list writes them.
  static constexpr std::string_view members = constant_text_v<Members>;
};

// What binding_of() reflects for a function that cannot be bound: the reason,
// as arguments_diagnostic() words it. It is recorded where the declarations
// were read, so that the entry point that reports it does not read them again
// at a point of instantiation, which may see more of them.
template <std::meta::info Diagnostic> struct unbound_function {};

template <typename Binding> inline constexpr bool is_bound_v = true;

template <std::meta::info Diagnostic>
inline constexpr bool is_bound_v<unbound_function<Diagnostic>> = false;

// One tool function as a registration sees it: how the function is bound, and
// the two facts that only a tool has. Its tool name and description come from
// the function's annotations, which accumulate across redeclarations, so they
// are template arguments for the reason bound_function gives.
template <typename Binding, std::meta::info Name, std::meta::info Description>
struct bound_tool {
  using binding = Binding;
  static constexpr std::string_view name = constant_text_v<Name>;
  static constexpr std::string_view description = constant_text_v<Description>;
};

// The tools one binding call binds, as bound_tool types, in declaration order. The set
// is part of the registration's identity too: a namespace can hold different functions
// in different translation units, and a class's member functions can gain annotations,
// including the tool annotation itself, from an out-of-class definition that only some
// units see.
template <typename... Tools> struct tool_set {};

// The argument object synthesized from a parameter list: one required member per
// value parameter, named as `Binding` records, of the parameter's type without
// references or cv-qualifiers. The consteval block runs when the specialization
// is instantiated, which C++26 permits because the aggregate it completes is a
// member of that same specialization. The names are read one character at a
// time, for the reason owned_text gives.
template <typename Binding> struct synthesized_arguments {
  struct type;

  consteval {
    std::vector<std::meta::info> members{};
    std::size_t position = 0;
    for (const auto parameter :
         value_parameters_of(Binding::function, Binding::context)) {
      std::string name{};
      for (; Binding::members[position] != ','; ++position) {
        name.push_back(Binding::members[position]);
      }
      ++position;
      std::meta::data_member_options options{};
      options.name = name;
      members.push_back(std::meta::data_member_spec(
          plain_type(std::meta::type_of(parameter)), options));
    }
    std::meta::define_aggregate(^^type, members);
  }
};

// Instantiates synthesized_arguments only for the form that needs it, and only
// for a function that has passed function_problem.
template <typename Binding> consteval std::meta::info arguments_type() {
  constexpr auto form = arguments_form_of(Binding::function, Binding::context);
  if constexpr (form == arguments_form::none) {
    return ^^no_arguments;
  } else if constexpr (form == arguments_form::aggregate) {
    return plain_type(std::meta::type_of(
        value_parameters_of(Binding::function, Binding::context).front()));
  } else {
    return ^^typename synthesized_arguments<Binding>::type;
  }
}

// The argument object of a binding, which arguments_t names. A class rather
// than an alias of the splice, so that apply() can spell the type in its
// signature: GCC 16 cannot mangle a function template whose signature contains
// a splice.
template <typename Binding> struct function_arguments {
  using type = typename[:arguments_type<Binding>():];
};

// A function that cannot be bound fails here with the recorded reason. The type
// it names all the same keeps the failure to that one error.
template <std::meta::info Diagnostic>
struct function_arguments<unbound_function<Diagnostic>> {
  static_assert(constant_text_v<Diagnostic>.empty(), constant_text_v<Diagnostic>);
  using type = no_arguments;
};

template <typename Binding>
using function_arguments_t = typename function_arguments<Binding>::type;

// ---- Diagnostics -------------------------------------------------------------

// The name a diagnostic calls an entity by: qualified through named namespaces
// and classes, without the signature display_string_of adds to a function.
consteval std::string entity_name(const std::meta::info entity) {
  const auto own = std::meta::has_identifier(entity)
                       ? owned_text(std::meta::identifier_of(entity))
                       : display(entity);
  if (entity == ^^::) {
    return own;
  }
  const auto parent = std::meta::parent_of(entity);
  if (std::meta::is_type(parent)) {
    return display(parent) + "::" + own;
  }
  if (std::meta::is_namespace(parent) && parent != (^^::) &&
      std::meta::has_identifier(parent)) {
    return entity_name(parent) + "::" + own;
  }
  return own;
}

consteval std::string tool_annotation_problem(const std::meta::info function) {
  if (annotation_count(function, ^^tool) > 1) {
    return "has more than one scrydantic::tool annotation";
  }
  if (annotation_count(function, ^^name) > 1) {
    return "has more than one scrydantic::name annotation";
  }
  if (has_annotation(function, ^^description) || has_annotation(function, ^^tag) ||
      has_annotation(function, ^^skip_null_t) ||
      has_annotation(function, ^^emit_null_t) ||
      has_annotation(function, ^^ignore_unknown_t)) {
    return "carries a codec annotation (description, tag, skip_null, emit_null, or "
           "ignore_unknown), which applies to data members or classes; a tool's "
           "description is its scrydantic::tool text";
  }
  if (has_annotation(function, ^^flatten_t)) {
    return "carries scrydantic::flatten, which applies to data members";
  }
  for (const auto annotation : std::meta::annotations_of(function)) {
    if (is_constraint(annotation)) {
      return "carries " + owned_text(constraint_name(annotation)) +
             ", which applies to data members";
    }
    if (annotation_matches(annotation, ^^check_t)) {
      return "carries scrydantic::check, which applies to data members and classes";
    }
  }
  if (!has_annotation(function, ^^name) && !std::meta::has_identifier(function)) {
    return "has no identifier to name the tool after; give it a "
           "scrydantic::name annotation";
  }
  return {};
}

// A member tool with an explicit object parameter is called on the toolbox its
// binding holds, an lvalue that is const when the toolbox is. That lvalue must
// initialize the parameter, as for any argument: a reference to the toolbox
// class or a base of it binds directly, a by-value parameter copies the toolbox,
// and a parameter of another type needs an implicit conversion.
consteval std::string explicit_object_problem(const std::meta::info function,
                                              const std::meta::info toolbox,
                                              const bool const_toolbox) {
  const auto declared = std::meta::type_of(std::meta::parameters_of(function).front());
  const auto type = std::meta::dealias(declared);
  const auto referred = std::meta::remove_reference(type);
  if (std::meta::is_rvalue_reference_type(type)) {
    return "takes its explicit object parameter by rvalue reference, so the toolbox "
           "object the tools are bound to cannot call it";
  }
  if (std::meta::is_lvalue_reference_type(type) && const_toolbox &&
      !std::meta::is_const_type(referred)) {
    return "takes its explicit object parameter by non-const reference, and the "
           "toolbox is const";
  }
  const auto object = const_toolbox ? std::meta::add_const(toolbox) : toolbox;
  if (std::meta::is_convertible_type(std::meta::add_lvalue_reference(object), type)) {
    return {};
  }
  if (!std::meta::is_reference_type(type) && plain_type(type) == toolbox) {
    return "takes its explicit object parameter by value, and " + display(toolbox) +
           " cannot be copy-initialized from an lvalue of " + display(object);
  }
  return "takes its explicit object parameter as " + display(declared) +
         ", which an lvalue of " + display(object) + " does not convert to";
}

// The functions that no binding can call.
consteval std::string uncallable_problem(const std::meta::info function) {
  if (std::meta::is_deleted(function)) {
    return "is deleted";
  }
  if (std::meta::is_constructor(function) || std::meta::is_destructor(function)) {
    return "is a constructor or destructor";
  }
  return {};
}

// `toolbox` is the class a member tool is bound to, or the null reflection for a
// function bound on its own.
consteval std::string binding_problem(const std::meta::info function,
                                      const std::meta::info toolbox,
                                      const bool const_toolbox) {
  if (auto reason = uncallable_problem(function); !reason.empty()) {
    return reason;
  }
  const bool bound =
      std::meta::is_class_member(function) && !std::meta::is_static_member(function);
  if (toolbox == std::meta::info{}) {
    return bound ? "is a non-static member function; bind an object of " +
                       display(std::meta::parent_of(function)) + " as a toolbox"
                 : std::string{};
  }
  if (!std::meta::is_public(function)) {
    return "is not a public member function";
  }
  if (has_explicit_object(function)) {
    return explicit_object_problem(function, toolbox, const_toolbox);
  }
  if (bound && std::meta::is_rvalue_reference_qualified(function)) {
    return "is &&-qualified, so the toolbox object the tools are bound to cannot "
           "call it";
  }
  if (bound && const_toolbox && !std::meta::is_const(function)) {
    return "is not a const member function, and the toolbox is const";
  }
  return {};
}

consteval std::string parameter_label(const std::meta::info parameter,
                                      const std::size_t index) {
  return std::meta::has_identifier(parameter)
             ? "parameter `" + owned_text(std::meta::identifier_of(parameter)) + "`"
             : "parameter " + decimal(index + 1U);
}

// Checks each parameter's passing convention: the decoded arguments are moved
// into the call, so a parameter may take a value, a const reference, or an
// rvalue reference, and the call context may appear only first. Parameters are
// numbered as declared, counting an explicit object parameter.
consteval std::string passing_problem(const std::meta::info function,
                                      const std::meta::info context) {
  const auto parameters = std::meta::parameters_of(function);
  for (std::size_t index = first_value_index(function, context);
       index < parameters.size(); ++index) {
    const auto type = std::meta::dealias(std::meta::type_of(parameters[index]));
    const auto label = parameter_label(parameters[index], index);
    if (has_context(context) && plain_type(type) == plain_type(context)) {
      return "has " + label + " of type " + display(plain_type(context)) +
             "; the context is only ever the leading parameter, as " +
             display(context_parameter_type(context));
    }
    const auto referred = std::meta::remove_reference(type);
    if ((std::meta::is_lvalue_reference_type(type) &&
         !std::meta::is_const_type(referred)) ||
        std::meta::is_volatile_type(referred)) {
      return "has " + label + " of type " +
             display(std::meta::type_of(parameters[index])) +
             ", which the decoded arguments cannot bind; take it by value or by "
             "const reference";
    }
  }
  return {};
}

// Rejects a Scrydantic annotation on a parameter the model supplies, which no
// binding reads: an argument object is synthesized from the parameters' types
// and names alone, the facts that bound_function keys, and annotations, which
// accumulate across redeclarations, would be one more fact that two translation
// units can see differently. Unread, a constraint or a check there would be
// silently lost, so a constrained argument is a member of an argument aggregate.
consteval std::string parameter_annotation_problem(const std::meta::info function,
                                                   const std::meta::info context) {
  const auto parameters = std::meta::parameters_of(function);
  for (std::size_t index = first_value_index(function, context);
       index < parameters.size(); ++index) {
    for (const auto annotation : std::meta::annotations_of(parameters[index])) {
      if (std::meta::parent_of(plain_type(std::meta::type_of(annotation))) ==
          (^^::scrydantic)) {
        return "has " + parameter_label(parameters[index], index) +
               " carrying a Scrydantic annotation, which a parameter cannot hold; "
               "take an argument aggregate and annotate its member";
      }
    }
  }
  return {};
}

consteval std::string arguments_problem(const std::meta::info function,
                                        const std::meta::info context) {
  const auto values = value_parameters_of(function, context);
  const std::size_t first = first_value_index(function, context);
  if (arguments_form_of(function, context) == arguments_form::aggregate) {
    const auto reason =
        tool_arguments_problem(plain_type(std::meta::type_of(values.front())));
    return reason.empty()
               ? std::string{}
               : "has " + parameter_label(values.front(), first) + ": " + reason;
  }
  for (std::size_t index = 0; index < values.size(); ++index) {
    const auto label = parameter_label(values[index], first + index);
    if (!std::meta::has_identifier(values[index])) {
      return "has the unnamed " + label +
             "; an argument synthesized from a parameter is named after it";
    }
    const auto reason =
        value_problem(std::meta::type_of(values[index]), value_family::supported);
    if (!reason.empty()) {
      return "has " + label + ": " + reason;
    }
  }
  // Each parameter passed the check alone, as its own schema's root. In the
  // synthesized object a recursive parameter type is a definition instead, and
  // all the parameters' definitions share one $defs, so their names are checked
  // together.
  std::vector<schema_node> held{};
  for (const auto value : values) {
    append_held_nodes(held, std::meta::type_of(value));
  }
  const auto reason = definition_names_problem(recursive_nodes(std::move(held), {}));
  return reason.empty() ? std::string{} : "has, among its parameters, " + reason;
}

// Why a function's parameter list cannot form an argument object; empty when it
// can. Every binding of a function applies these rules to its parameters.
consteval std::string parameters_problem(const std::meta::info function,
                                         const std::meta::info context) {
  auto reason = passing_problem(function, context);
  if (reason.empty()) {
    reason = parameter_annotation_problem(function, context);
  }
  if (reason.empty()) {
    reason = arguments_problem(function, context);
  }
  return reason;
}

// Why arguments_t and apply() cannot bind an entity, prefixed with its name;
// empty when they can. The function's own annotations and what it returns are
// its caller's business, and so is the object that calls a non-static member
// function.
consteval std::string function_problem(const std::meta::info entity,
                                       const std::meta::info context) {
  if (std::meta::is_function_template(entity)) {
    return display(entity) + " is a function template; a binding is of one function "
                             "with one argument object";
  }
  if (!std::meta::is_function(entity)) {
    return display(entity) + " is not a function";
  }
  auto reason = uncallable_problem(entity);
  if (reason.empty()) {
    reason = parameters_problem(entity, context);
  }
  return reason.empty() ? std::string{} : entity_name(entity) + " " + reason;
}

// Why a function cannot be bound as a tool, prefixed with its name; empty when
// it can. It must already carry a tool annotation.
consteval std::string tool_function_problem(const std::meta::info function,
                                            const std::meta::info toolbox,
                                            const bool const_toolbox,
                                            const std::meta::info context) {
  auto reason = tool_annotation_problem(function);
  if (reason.empty()) {
    reason = binding_problem(function, toolbox, const_toolbox);
  }
  if (reason.empty()) {
    reason = parameters_problem(function, context);
  }
  if (reason.empty()) {
    const auto result = result_type_problem(std::meta::return_type_of(function));
    reason = result.empty() ? std::string{} : "returns " + result;
  }
  return reason.empty() ? std::string{} : entity_name(function) + " " + reason;
}

// Rejects a tool annotation on anything a walk finds that is not a function.
consteval std::string misplaced_tool_problem(const std::meta::info scope) {
  for (const auto member :
       std::meta::members_of(scope, std::meta::access_context::unchecked())) {
    if (!std::meta::is_function(member) && is_annotated(member, ^^tool)) {
      return entity_name(member) +
             " carries scrydantic::tool, which applies to functions";
    }
  }
  return {};
}

consteval std::string
duplicate_names_problem(const std::vector<std::meta::info>& tools) {
  for (std::size_t left = 0; left < tools.size(); ++left) {
    for (std::size_t right = left + 1; right < tools.size(); ++right) {
      const auto shared = tool_name_of(tools[left]);
      if (shared == tool_name_of(tools[right])) {
        return display(tools[left]) + " and " + display(tools[right]) +
               " are both tools named \"" + owned_text(shared) + "\"";
      }
    }
  }
  return {};
}

// The checks a class or namespace of tools shares. `toolbox` is the class the
// tools bind to, or the null reflection for a namespace.
consteval std::string tool_scope_problem(const std::meta::info scope,
                                         const std::meta::info toolbox,
                                         const bool const_toolbox,
                                         const std::meta::info context) {
  if (auto misplaced = misplaced_tool_problem(scope); !misplaced.empty()) {
    return misplaced;
  }
  const auto tools = annotated_functions_of(scope, ^^tool);
  if (tools.empty()) {
    return (toolbox == std::meta::info{} ? "namespace " : "") + entity_name(scope) +
           " declares no function annotated with scrydantic::tool";
  }
  for (const auto function : tools) {
    if (auto reason = tool_function_problem(function, toolbox, const_toolbox, context);
        !reason.empty()) {
      return reason;
    }
  }
  return duplicate_names_problem(tools);
}

consteval std::string toolbox_problem(const std::meta::info type,
                                      const std::meta::info context) {
  const auto toolbox = plain_type(type);
  if (!std::meta::is_class_type(toolbox)) {
    return display(type) + " is not a class type";
  }
  if (!is_complete_class(toolbox)) {
    return display(type) + " is an incomplete type";
  }
  return tool_scope_problem(toolbox, toolbox,
                            std::meta::is_const_type(std::meta::remove_reference(type)),
                            context);
}

consteval std::string entity_problem(const std::meta::info entity,
                                     const std::meta::info context) {
  if (std::meta::is_namespace(entity)) {
    return tool_scope_problem(entity, {}, false, context);
  }
  if (std::meta::is_function(entity)) {
    return is_annotated(entity, ^^tool)
               ? tool_function_problem(entity, {}, false, context)
               : entity_name(entity) + " has no scrydantic::tool annotation";
  }
  if (std::meta::is_type(entity)) {
    return display(entity) + " is a type; bind an object of it as a toolbox instead";
  }
  if (std::meta::is_function_template(entity)) {
    return display(entity) +
           " is a function template; a tool is one function with one schema";
  }
  return display(entity) + " is neither a function nor a namespace";
}

// ---- Bindings and tool sets ---------------------------------------------------

// A fact as a template argument: a constant string, which GCC mangles by its
// content.
consteval std::meta::info constant_text(const std::string_view text) {
  return std::meta::reflect_constant(
      std::meta::reflect_constant_string(owned_text(text)));
}

// The bound_function of a function that has passed function_problem, with the
// facts the declarations preceding this point give it.
consteval std::meta::info bound_function_of(const std::meta::info function,
                                            const std::meta::info context) {
  return std::meta::substitute(
      ^^bound_function,
      {
          std::meta::reflect_constant(function), std::meta::reflect_constant(context),
          constant_text(synthesized_member_list(function, context))});
}

// The bound_tool of a function that has passed tool_function_problem, with the
// facts the declarations preceding this point give it.
consteval std::meta::info bound_tool_of(const std::meta::info function,
                                        const std::meta::info context) {
  return std::meta::substitute(^^bound_tool,
                               {
                                   bound_function_of(function, context),
                                   constant_text(tool_name_of(function)),
                                   constant_text(tool_description_of(function))});
}

consteval std::meta::info tool_set_of(const std::vector<std::meta::info>& functions,
                                      const std::meta::info context) {
  std::vector<std::meta::info> tools{};
  for (const auto function : functions) {
    tools.push_back(bound_tool_of(function, context));
  }
  return std::meta::substitute(^^tool_set, tools);
}

// The diagnostic of a binding of `entity` through `api`, the function the caller
// called, such as "scrydantic::bind": the call, then the reason. Empty when the
// entity binds, which an empty `reason` says.
consteval std::string call_diagnostic(const std::string_view api,
                                      const std::meta::info entity,
                                      const std::string& reason) {
  if (reason.empty()) {
    return {};
  }
  const auto shown = std::meta::is_type(entity) ? display(entity) : entity_name(entity);
  return owned_text(api) + "<^^" + shown + ">(): " + reason;
}

template <std::meta::info Entity, typename Context>
consteval std::string_view entity_tools_diagnostic(const std::string_view api) {
  const auto text = call_diagnostic(api, Entity, entity_problem(Entity, ^^Context));
  return text.empty() ? std::string_view{} : static_string(text);
}

// Why arguments_t and apply() cannot bind `Function`, as a call through `api`.
template <std::meta::info Function, typename Context>
consteval std::string_view function_arguments_diagnostic(const std::string_view api) {
  const auto text =
      call_diagnostic(api, Function, function_problem(Function, ^^Context));
  return text.empty() ? std::string_view{} : static_string(text);
}

template <typename Type, typename Context>
consteval std::string_view toolbox_diagnostic() {
  return unsatisfied(display(^^Type), "Toolbox", toolbox_problem(^^Type, ^^Context));
}

// The type-erased handler that ToolFunction<Context> names in bound_tool.hpp. A
// trait rather than a conditional type, because a handler type that took a
// `const void&` could not even be named. Its call operator is const, so a const
// table of tools can be called, and it calls what it stores as const.
template <typename Context> struct tool_function {
  using type = std::move_only_function<Result<Json>(const Context&, Json) const>;
};

template <> struct tool_function<void> {
  using type = std::move_only_function<Result<Json>(Json) const>;
};

} // namespace scrydantic::detail
