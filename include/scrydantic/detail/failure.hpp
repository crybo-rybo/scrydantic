#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <ranges>
#include <scrydantic/error.hpp>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

namespace scrydantic::detail {

// `<path> <reason>`, such as `$.shapes[0].radius must be a number`.
[[nodiscard]] inline std::string located(const std::string_view path,
                                         const std::string_view reason) {
  std::string text{path};
  text.push_back(' ');
  text.append(reason);
  return text;
}

// ---- Encoding ----------------------------------------------------------------

// An encode failure on its way out of the recursion. Encoding stops at the first
// failure, so the call that fails knows only the reason; each enclosing member or
// element appends its own step to `path` as the failure passes it, innermost
// first, and a successful encode spends nothing on paths.
struct encode_failure {
  std::string reason{};
  std::vector<PathSegment> path{};
};

template <typename Type> using encode_result = std::expected<Type, encode_failure>;

[[nodiscard]] inline std::unexpected<encode_failure> encode_fail(std::string reason) {
  return std::unexpected(encode_failure{.reason = std::move(reason)});
}

[[nodiscard]] inline encode_failure at_member(encode_failure failure,
                                              const std::string_view key) {
  failure.path.emplace_back(std::in_place_type<std::string>, key);
  return failure;
}

[[nodiscard]] inline encode_failure at_element(encode_failure failure,
                                               const std::size_t index) {
  failure.path.emplace_back(index);
  return failure;
}

// A host-only diagnostic: the failure describes the program's own value, whose
// schema the model never sees, so nothing about it is text the model could act
// on, and it is no decode issue.
[[nodiscard]] inline Error encode_failure_error(encode_failure failure) {
  std::ranges::reverse(failure.path);
  return Error{.message = located(json_path(failure.path), failure.reason)};
}

// ---- Decoding ----------------------------------------------------------------

// The issues one decode found, in the order it found them. `stopped` is set at
// the first issue past max_issues, which is not kept; decoding ends there.
struct decode_report {
  std::vector<Issue> issues{};
  // The host's reason for each issue whose reason the model is not given in
  // full, by the issue's index: a validator's `message`, where the issue's
  // reason is its `model_message`. Few decodes have one, so they are kept apart.
  std::vector<std::pair<std::size_t, std::string>> host_reasons{};
  bool stopped{};
};

// What the model is told of a validator's rejection that gives it no text: that
// the value was rejected, and nothing of why.
inline constexpr std::string_view unexplained_rejection = "is not valid";

// Why a value failed to decode: its issues are already in the decode_report, so
// the failure carries nothing else. It is an enumeration rather than an empty
// struct because at -O2 GCC 16 warns (-Wmaybe-uninitialized) that std::expected
// copies the never-written byte of an empty error type.
enum class decode_failure : std::uint8_t {
  reported,
};

template <typename Type> using decode_result = std::expected<Type, decode_failure>;

// A failed decode_result of `Type`, built with std::unexpect rather than
// converted from a std::unexpected. libstdc++ 16's converting constructor of
// std::expected asks whether `Type` is constructible from its argument before it
// rules out a std::unexpected. For an aggregate whose first member is
// std::optional<std::indirect<Self>>, that question asks itself, through the
// constraints of the forwarding constructors of std::optional and std::indirect,
// which is a hard error. A variant with such an alternative asks it too, through
// the copy-list-initialization that selects its converting constructor's
// alternative. Aggregates are therefore read in place, reporting through the
// cursor, and read_in_place() builds every failed result of one with this, as
// the variant reader builds its own. The readers of optionals and indirects
// build their results with transform(), and the decode entry points with
// transform_error() and and_then(), which use std::unexpect themselves. The
// other readers return fail()'s result, which cannot ask the question: a scalar
// or a string has no first member, std::vector has no forwarding constructor,
// and a std::array copy-initializes its element, which does not
// aggregate-initialize.
template <typename Type> [[nodiscard]] decode_result<Type> decode_failed() {
  return decode_result<Type>{std::unexpect, decode_failure::reported};
}

// Where the decoder is: the path from the root to the value being read, and the
// report its issues go to. Each cursor lives in the call that descends through
// its step and points at its parent's, so the path is a chain through the
// recursion that costs nothing until an issue copies it.
class decode_cursor {
public:
  explicit decode_cursor(decode_report& report) noexcept : report_{&report} {}

  // The cursor of a member or element of this cursor's value. The child points
  // at this cursor, so it must not outlive it; the rvalue overloads are deleted
  // so that a chain of temporaries cannot leave a dangling parent behind. `key`
  // must be a static string (fields_v, tag_key), never text of the document.
  [[nodiscard]] decode_cursor member(const std::string_view key) const& noexcept {
    return decode_cursor{report_, this, key, 0};
  }
  decode_cursor member(std::string_view key) const&& = delete;

  [[nodiscard]] decode_cursor element(const std::size_t index) const& noexcept {
    return decode_cursor{report_, this, {}, index};
  }
  decode_cursor element(std::size_t index) const&& = delete;

  // Whether the report is full, so decoding must stop.
  [[nodiscard]] bool stopped() const noexcept { return report_->stopped; }

  // Reports an issue at this cursor's value. Past max_issues it only stops the
  // decode, without building the issue.
  void report(const IssueKind kind, const std::string_view reason) const {
    if (admit()) {
      report_->issues.push_back(
          Issue{.kind = kind, .path = path(), .reason = std::string{reason}});
    }
  }

  // Reports a validator's rejection of this cursor's value. The issue's reason
  // is what the model may see, the rejection's model_message, and the host's own
  // text, its message, is kept beside it when the two differ.
  void reject(const Error& rejection) const {
    if (!admit()) {
      return;
    }
    const std::string_view model = rejection.model_message.empty()
                                       ? unexplained_rejection
                                       : std::string_view{rejection.model_message};
    const std::string_view host =
        rejection.message.empty() ? model : std::string_view{rejection.message};
    if (host != model) {
      report_->host_reasons.emplace_back(report_->issues.size(), host);
    }
    report_->issues.push_back(Issue{
        .kind = IssueKind::failed_check, .path = path(), .reason = std::string{model}});
  }

  // Reports an issue and fails the value it is about.
  [[nodiscard]] std::unexpected<decode_failure>
  fail(const IssueKind kind, const std::string_view reason) const {
    report(kind, reason);
    return std::unexpected(decode_failure::reported);
  }

private:
  decode_cursor(decode_report* const report, const decode_cursor* const parent,
                const std::string_view key, const std::size_t index) noexcept
      : report_{report}, parent_{parent}, key_{key}, index_{index} {}

  // Whether the report keeps one more issue. The first issue past max_issues is
  // not kept and stops the decode instead.
  [[nodiscard]] bool admit() const noexcept {
    if (report_->issues.size() < max_issues) {
      return true;
    }
    report_->stopped = true;
    return false;
  }

  // The owned path of this cursor's value, collected from the innermost step out.
  [[nodiscard]] std::vector<PathSegment> path() const {
    std::vector<PathSegment> segments{};
    for (const decode_cursor* cursor = this; cursor->parent_ != nullptr;
         cursor = cursor->parent_) {
      if (cursor->key_.data() != nullptr) {
        segments.emplace_back(std::in_place_type<std::string>, cursor->key_);
      } else {
        segments.emplace_back(cursor->index_);
      }
    }
    std::ranges::reverse(segments);
    return segments;
  }

  decode_report* report_{};
  // Null at the root, which is no step.
  const decode_cursor* parent_{};
  // The step from the parent's value: a member's key, or an element's index when
  // `key_` is a null view, which no static string is. Two plain members rather
  // than a std::variant: GCC 16 does not inline member() into the larger readers
  // when it constructs a variant, and the calls cost 4% of a decode.
  std::string_view key_{};
  std::size_t index_{};
};

// One `<path> <reason>` line per issue, each with the reason `reason_of` gives
// its index, then the line that says the decode stopped, if it did.
template <typename Reason>
[[nodiscard]] std::string issue_lines(const decode_report& report, Reason reason_of) {
  auto text = std::views::iota(0UZ, report.issues.size()) |
              std::views::transform([&](const std::size_t index) {
                return located(json_path(report.issues[index].path), reason_of(index));
              }) |
              std::views::join_with('\n') | std::ranges::to<std::string>();
  if (report.stopped) {
    text.append("\nfurther issues past the first " + std::to_string(max_issues) +
                " were not reported");
  }
  return text;
}

// A decode failure. Every word of an issue's reason comes from the schema the
// model was already given, from the input itself, or from a validator's
// model_message, so the model is told the reasons, and the host the same lines
// but with each validator's own message where it gave one.
[[nodiscard]] inline Error decode_error(decode_report report) {
  auto model_text =
      issue_lines(report, [&](const std::size_t index) -> std::string_view {
        return report.issues[index].reason;
      });
  auto host_text =
      report.host_reasons.empty()
          ? model_text
          : issue_lines(report, [&](const std::size_t index) -> std::string_view {
              const auto host =
                  std::ranges::find(report.host_reasons, index,
                                    &std::pair<std::size_t, std::string>::first);
              return host == report.host_reasons.end() ? report.issues[index].reason
                                                       : host->second;
            });
  return Error{
      .message = std::move(host_text),
      .model_message = std::move(model_text),
      .issues = std::move(report.issues),
  };
}

// Text that is not JSON: one issue at the root. `message` names the input, as
// the operation that read it calls it.
[[nodiscard]] inline Error invalid_json_error(std::string message) {
  return Error{
      .message = message,
      .model_message = std::move(message),
      .issues = {Issue{.kind = IssueKind::invalid_json, .reason = "is not valid JSON"}},
  };
}

} // namespace scrydantic::detail
