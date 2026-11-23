#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <ranges>
#include <scrydantic/decode_options.hpp>
#include <scrydantic/error.hpp>
#include <scrydantic/json.hpp>
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
  // Whether `reason` is the whole message, about the value as a whole, rather
  // than what is wrong at `path`.
  bool whole{};
};

template <typename Type> using encode_result = std::expected<Type, encode_failure>;

[[nodiscard]] inline std::unexpected<encode_failure> encode_fail(std::string reason) {
  return std::unexpected(encode_failure{.reason = std::move(reason)});
}

// A failure that says only that the value as a whole cannot be JSON. Invalid
// UTF-8 and payloads nested too deep read this way, without a path, as they did
// when a check of the finished text found them.
[[nodiscard]] inline std::unexpected<encode_failure>
encode_fail_whole(std::string message) {
  return std::unexpected(encode_failure{.reason = std::move(message), .whole = true});
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
  if (failure.whole) {
    return Error{.message = std::move(failure.reason)};
  }
  std::ranges::reverse(failure.path);
  return Error{.message = located(json_path(failure.path), failure.reason)};
}

// ---- Decoding ----------------------------------------------------------------

// A value that the text of a string spelled, for a decode whose options let the
// string stand for it, and the depth() of the cursor the string was read at.
struct spelled_value {
  std::size_t depth{};
  JsonView value{};
};

// The issues one decode found, in the order it found them. `stopped` is set at
// the first issue past max_issues, which is not kept; decoding ends there. The
// rest of what one decode shares is here too, because this is the one thing every
// cursor of the decode already points at, so carrying it costs a reader nothing
// until it asks: the options the decode was given, and the values that strings
// spelled, which decode_cursor::keep() explains.
struct decode_report {
  std::vector<Issue> issues{};
  // The host's reason for each issue whose reason the model is not given in
  // full, by the issue's index: a validator's `message`, where the issue's
  // reason is its `model_message`. Few decodes have one, so they are kept apart.
  std::vector<std::pair<std::size_t, std::string>> host_reasons{};
  bool stopped{};
  DecodeOptions options{};
  std::vector<spelled_value> spelled{};
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

  // The cursor of a map entry of this cursor's value. `key` is the entry's key
  // in the document being decoded, which outlives every cursor of the decode.
  // The document may hold an empty key as a null view, which would read here as
  // an element's step, so an empty key is given static storage.
  [[nodiscard]] decode_cursor entry(const std::string_view key) const& noexcept {
    return decode_cursor{report_, this, key.empty() ? std::string_view{""} : key, 0};
  }
  decode_cursor entry(std::string_view key) const&& = delete;

  [[nodiscard]] decode_cursor element(const std::size_t index) const& noexcept {
    return decode_cursor{report_, this, {}, index};
  }
  decode_cursor element(std::size_t index) const&& = delete;

  // Whether the report is full, so decoding must stop.
  [[nodiscard]] bool stopped() const noexcept { return report_->stopped; }

  // What the decode was asked to accept beyond its strict rules.
  [[nodiscard]] const DecodeOptions& options() const noexcept {
    return report_->options;
  }

  // How many arrays and objects are open around this cursor's value: the steps
  // of its path, each of which entered one.
  [[nodiscard]] std::size_t depth() const noexcept {
    std::size_t steps = 0;
    for (const decode_cursor* cursor = this; cursor->parent_ != nullptr;
         cursor = cursor->parent_) {
      ++steps;
    }
    return steps;
  }

  // Keeps `value`, which the text of the string at this cursor spelled, for as
  // long as the decode reads it, and returns the kept view. A value is read
  // under the cursor of its string and the cursors below that one, which are
  // deeper, and a string in it is at one of those. So the values still being
  // read were all kept at a lesser depth than this cursor's, and every other
  // is dropped here. What is kept is therefore one value for each depth at
  // most, however many strings the input holds.
  [[nodiscard]] const JsonView& keep(JsonView value) const {
    auto& kept = report_->spelled;
    const std::size_t level = depth();
    while (!kept.empty() && kept.back().depth >= level) {
      kept.pop_back();
    }
    return kept.emplace_back(level, std::move(value)).value;
  }

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
  // The step from the parent's value: a member's or a map entry's key, or an
  // element's index when `key_` is a null view, which no static string is and
  // entry() never keeps. Two plain members rather than a std::variant: GCC 16
  // does not inline member() into the larger readers when it constructs a
  // variant, and the calls cost 4% of a decode.
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

// A tool's result that misses a constraint of its own type or fails one of its
// validators, as `report` lists. The result is the tool's, and the fault its
// code's, so the model has no part in it and is told nothing: like a result that
// cannot be encoded, this is a host-only diagnostic, with no model_message and
// no issues. The host is given the lines a decode failure gives it, each with
// the validator's own message where it gave one, and each says that its path is
// a path in the result: `result $.v must be at most 10`.
//
// The lines are built here and not by issue_lines() with a subject to put
// before each. Every decode instantiates issue_lines() and decode_error(), and
// GCC 16 chooses what to inline across a whole translation unit, so a change to
// either gave the encoder other code in a unit that checks no result. As they
// stand, decoding and encoding compile to the instructions they compiled to
// before results were checked.
[[nodiscard]] inline Error invalid_result_error(const decode_report& report) {
  std::string message{};
  for (std::size_t index = 0; index < report.issues.size(); ++index) {
    const auto host = std::ranges::find(report.host_reasons, index,
                                        &std::pair<std::size_t, std::string>::first);
    message.append(index == 0 ? "result " : "\nresult ");
    message.append(located(json_path(report.issues[index].path),
                           host == report.host_reasons.end()
                               ? std::string_view{report.issues[index].reason}
                               : std::string_view{host->second}));
  }
  if (report.stopped) {
    message.append("\nfurther issues past the first " + std::to_string(max_issues) +
                   " were not reported");
  }
  return Error{.message = std::move(message)};
}

// Text that is not JSON: one issue at the root. `verdict` names the input as the
// operation that read it calls it, such as `tool arguments are not valid JSON`,
// and `description` is describe_invalid_json()'s, such as `expected ':' at line
// 1, column 7`. Both messages are `<verdict>: <description>`, and the issue's
// reason is `is not valid JSON: <description>`.
[[nodiscard]] inline Error invalid_json_error(const std::string_view verdict,
                                              const std::string_view description) {
  const auto explained = [description](const std::string_view what) {
    std::string text{what};
    text.append(": ");
    text.append(description);
    return text;
  };
  auto message = explained(verdict);
  return Error{
      .message = message,
      .model_message = std::move(message),
      .issues = {Issue{.kind = IssueKind::invalid_json,
                       .reason = explained("is not valid JSON")}},
  };
}

} // namespace scrydantic::detail
