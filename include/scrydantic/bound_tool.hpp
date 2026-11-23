#pragma once

#if !defined(__cpp_impl_reflection)
#error "Scrydantic requires a compiler implementing P2996 (GCC 16.2 or newer)"
#endif

#include <optional>
#include <scrydantic/detail/tools.hpp>
#include <scrydantic/error.hpp>
#include <scrydantic/json.hpp>
#include <string>
#include <string_view>

/// What a tool binding produces: a bound tool, its type-erased handler, and the
/// handler-free descriptor. <scrydantic/tools.hpp> includes this header and
/// declares the entry points that build them (bind(), bind_handler(), and
/// describe()), so most code includes that one instead.
namespace scrydantic {

/// Move-only type-erased handler of one bound tool, callable when const.
///
/// A const BoundTool, a const vector of them, and a const ToolSet can therefore
/// be called. A call keeps no state of its own beyond the callable or the
/// toolbox the handler shares: it decodes, calls, and encodes on values local to
/// the call. Concurrent calls of one handler are as safe as that callable's or
/// toolbox's own code; a toolbox whose tools change it, or a callable that
/// changes state it points to, needs its own synchronization for them.
///
/// A call passes the JSON argument object, preceded by a `const Context&` when
/// `Context` is not void, in one of two forms:
///
///     handler(arguments);                 // a JsonView
///     handler(Json{.text = R"({...})"});  // text
///
/// A view is for a host that has parsed a larger message with the arguments
/// somewhere inside it. The handler decodes that part of the host's document
/// where it lies: nothing is written out and nothing is parsed again. Text is
/// parsed first, and text that is not JSON fails with one issue and the message
/// `tool arguments are not valid JSON: ` followed by the reason and position
/// JsonView::parse() gives. A view is JSON already, so that failure is the text
/// call's alone. Otherwise the two calls answer alike: a view of something that
/// is not an object fails as text that spells it does, when the argument object
/// decodes.
///
/// Either call returns the JSON result, encoded as
/// encode_verbatim() encodes it: valid JSON with keys in canonical order, and
/// spliced `Json` payloads byte for byte. A failure is an Error of one of three
/// kinds. Arguments that fail to decode, including a validator's rejection, list
/// their issues in `issues` and describe them in `model_message`. A refusal is the
/// Error the tool returned, unchanged; one from tool_error() has empty `issues` and
/// the tool's text for the model in `model_message`. A result that cannot be JSON,
/// such as a string holding invalid UTF-8 or a NaN, is a host-side failure with
/// empty `issues` and `model_message` and the reason in `message`. A tool that
/// returns an Error with issues of its own, or with no model text, therefore reads
/// as the first or the last kind.
///
/// bind() and bind_handler() make handlers. One may also be written by hand, as
/// any callable that can be called when const with the arguments of one of the
/// two forms and returns a Result<Json>; a ToolFunction converts from it. A
/// callable that takes a `const JsonView&` reads the view of a view call and the
/// parsed text of a text call. A callable that takes `Json` is kept as it is:
/// it receives the text of a text call as passed, neither parsed nor checked,
/// and for a view call the view's canonical text, JsonView::to_json(), which is
/// written once per call. A default-constructed ToolFunction is empty, which
/// `if (!handler)` and a comparison with `nullptr` test; calling an empty one is
/// undefined behavior.
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
  /// Generated JSON Schema of what a successful call returns, or empty when the
  /// tool returns no value.
  ///
  /// bind() sets it to schema_v<T> for a function that returns `T` or
  /// `Result<T>`, and leaves it empty for one that returns `void` or Status,
  /// whose `{}` is an acknowledgment rather than a value with a contract. It is
  /// for a host to publish beside `input_schema`; `invoke` does not read it. A
  /// tool built by hand, such as one around a bind_handler() handler, may leave
  /// it empty or supply a schema of its own.
  std::optional<Json> output_schema{};
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
  /// Generated JSON Schema of what a successful call returns, as
  /// BoundTool::output_schema holds it, or empty when the tool returns no value.
  std::string_view output_schema{};
};

} // namespace scrydantic
