#pragma once

#include <cstddef>
#include <expected>
#include <scrydantic/error.hpp>
#include <string>
#include <string_view>
#include <utility>

namespace scrydantic::detail {

// A codec failure on its way out of the recursion. `path` is relative to the value
// the failing call was given (".blocks[2].text") and grows at the front as the
// failure passes each enclosing member or element, so a successful encode or decode
// spends nothing on paths.
struct codec_failure {
  std::string reason{};
  std::string path{};
};

template <typename Type> using codec_result = std::expected<Type, codec_failure>;

[[nodiscard]] inline std::unexpected<codec_failure> codec_fail(std::string reason) {
  return std::unexpected(codec_failure{.reason = std::move(reason)});
}

[[nodiscard]] inline codec_failure at_member(codec_failure failure,
                                             const std::string_view key) {
  std::string prefix{"."};
  prefix.append(key);
  failure.path.insert(0, prefix);
  return failure;
}

[[nodiscard]] inline codec_failure at_element(codec_failure failure,
                                              const std::size_t index) {
  failure.path.insert(0, "[" + std::to_string(index) + "]");
  return failure;
}

// `<path> <reason>`, such as `$.shapes[0].radius must be a number`.
[[nodiscard]] inline std::string located(const std::string_view path,
                                         const std::string_view reason) {
  std::string text{path};
  text.push_back(' ');
  text.append(reason);
  return text;
}

// A host-only codec diagnostic. Encoding uses this shape: the failure describes
// the program's own value, whose schema the model never sees, so nothing about
// it is text the model could act on.
[[nodiscard]] inline Error codec_error(const std::string_view path,
                                       const std::string_view reason) {
  return Error{.message = located(path, reason)};
}

// A decode diagnostic. Every word of this text is derived from the schema the
// model was already given, so the model receives the host's text unchanged.
[[nodiscard]] inline Error decode_error(const std::string_view path,
                                        const std::string_view reason) {
  auto text = located(path, reason);
  return Error{.message = text, .model_message = std::move(text)};
}

[[nodiscard]] inline Error encode_failure_error(const codec_failure& failure) {
  return codec_error("$" + failure.path, failure.reason);
}

[[nodiscard]] inline Error decode_failure_error(const std::string_view root,
                                                const codec_failure& failure) {
  return decode_error(std::string{root} + failure.path, failure.reason);
}

} // namespace scrydantic::detail
