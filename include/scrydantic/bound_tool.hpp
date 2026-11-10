#pragma once

#if !defined(__cpp_impl_reflection)
#error "Scrydantic requires a compiler implementing P2996 (GCC 16.2 or newer)"
#endif

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
/// It receives the canonical JSON argument object, preceded by a `const
/// Context&` when `Context` is not void, and returns the JSON result, encoded as
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

} // namespace scrydantic
