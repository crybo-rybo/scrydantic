#pragma once

#include <cstddef>
#include <cstdint>
#include <expected>
#include <span>
#include <string>
#include <utility>
#include <variant>
#include <vector>

namespace scrydantic {

/// What kind of failure one decode Issue is.
enum class IssueKind : std::uint8_t {
  /// The input text is not one JSON document, so nothing was decoded. The
  /// issue is at the root, and its reason says why and where reading stopped,
  /// such as `is not valid JSON: expected ':' at line 1, column 7`, as
  /// JsonView::parse() describes.
  invalid_json,
  /// A JSON value of a type the schema does not allow there, such as a string
  /// where a number is required.
  wrong_type,
  /// A number outside the range of its integer or floating-point type.
  out_of_range,
  /// A required object member is absent.
  missing_member,
  /// An object holds a member its schema does not declare. The issue is at the
  /// object, and its reason names the member.
  unknown_member,
  /// A string that names none of an enumeration's declared enumerators.
  unknown_enumerator,
  /// A tagged variant's object has no `type` member, so no alternative was
  /// chosen and none of its other members were checked.
  missing_tag,
  /// A tagged variant's `type` member is not a string naming one of its
  /// declared tags, so no alternative was chosen and none of its other members
  /// were checked.
  unknown_tag,
  /// An array whose length is not its fixed-size type's.
  wrong_size,
  /// A value outside a constraint that its member declares with
  /// scrydantic::range, multiple_of, length, or items. The reason states the
  /// bound as the schema's keyword means it, such as `must be at most 150`.
  violated_constraint,
  /// A value that a scrydantic::check validator rejected. The reason is the
  /// validator's `model_message`, or `is not valid` when it gave the model none;
  /// its `message` is only ever in Error::message.
  failed_check,
};

/// One step of the path from a decoded value to an issue in it: the key of an
/// object member, or the zero-based index of an array element.
using PathSegment = std::variant<std::string, std::size_t>;

/// One way a decoded value failed its type, and where.
struct Issue {
  /// What kind of failure it is.
  IssueKind kind{};
  /// Where it is, from the root of the decoded value outwards; empty for the
  /// root itself. json_path() spells it.
  std::vector<PathSegment> path{};
  /// What the value at `path` failed to be, worded to follow the path in a
  /// sentence, such as `must be a number`. It is text a model may see: words of
  /// the schema or of the input, or a validator's `model_message`.
  std::string reason{};

  /// Compares every member.
  /// @return Whether both issues are the same failure at the same path.
  [[nodiscard]] bool operator==(const Issue&) const = default;
};

/// The most issues one decode reports.
///
/// A model or a person can act on this many at once, and it bounds the work and
/// the text that hostile input can cause: decoding stops at the first issue past
/// this many, so an array of a million wrong elements costs as much as one of
/// 65.
inline constexpr std::size_t max_issues = 64;

/// Spells a path as JSONPath, rooted at the decoded value.
///
/// The root is `$`. A member whose key is an ASCII identifier (a letter or `_`,
/// then letters, digits, and `_`) follows as `.key`. Any other key follows as
/// `["key"]`, the key spelled as a JSON string literal, so a key holding `.`,
/// `[`, or `"` cannot be mistaken for more steps. An element follows as
/// `[index]`. For example, `$.shapes[0].radius` or `$.headers["content-type"]`.
/// @param path Segments from the root outwards.
/// @return The path's text.
[[nodiscard]] std::string json_path(std::span<const PathSegment> path);

/// Error value of every fallible Scrydantic operation.
///
/// Scrydantic reports what failed and where, and for a decode, what kind of
/// failure each issue is. It leaves the rest to the caller, which knows whether
/// the input came from a user, a model, or a peer.
struct Error {
  /// Human-readable diagnostic for the host.
  ///
  /// A decode failure writes one `<path> <reason>` line per issue, such as
  /// `$.shapes[0].radius must be a number`, joined by newlines, then a last line
  /// saying that further issues were not reported when decoding stopped past
  /// max_issues. The line of an IssueKind::failed_check issue gives the
  /// validator's own `message` as its reason, where it has one. Text that is not
  /// JSON is one sentence instead, naming the input and saying why and where
  /// reading stopped, such as
  /// `JSON text is not valid JSON: expected ':' at line 1, column 7`.
  std::string message{};
  /// Text meant for a model, or empty when the failure carries nothing a model
  /// could act on. Scrydantic fills it for strict decode failures with the same
  /// lines as `message`, but each with its issue's reason, whose every word comes
  /// from a schema the model was already given, from the input, or from a
  /// validator's own `model_message`. So it differs from `message` only where a
  /// validator gave the host text of its own. For text that is not JSON, from
  /// JsonView::parse() too, it is `message`. A tool handler fills it through
  /// tool_error().
  std::string model_message{};
  /// The issues a decode found, in the order it visited them: an object's
  /// unknown members in key order, then its declared members in declaration
  /// order, and an array's elements in index order. It holds at most max_issues.
  /// Every other failure, encoding included, leaves it empty.
  std::vector<Issue> issues{};
};

/// Builds the error a tool handler returns to show the model why its call failed.
///
/// An empty `host_message` copies `model_message` into `Error::message`, so a
/// handler that wants one diagnostic writes it once.
/// @param model_message Text meant for the model.
/// @param host_message Host-side diagnostic kept in Error::message; empty copies
/// model_message.
/// @return An Error carrying both texts.
[[nodiscard]] inline Error tool_error(std::string model_message,
                                      std::string host_message = {}) {
  // A handler that supplies no host text still wants the refusal in its own logs,
  // and repeating text it already chose to show the model discloses nothing new.
  if (host_message.empty()) {
    host_message = model_message;
  }
  return Error{
      .message = std::move(host_message),
      .model_message = std::move(model_message),
  };
}

/// Result of a fallible Scrydantic operation.
template <typename T> using Result = std::expected<T, Error>;

/// Result of a fallible operation that returns no value on success.
using Status = Result<void>;

} // namespace scrydantic
