#include "json/core.hpp" // IWYU pragma: keep

#include "json/document.hpp"

#include <cstddef>
#include <cstdint>
#include <expected>
#include <memory>
#include <optional>
#include <scrydantic/detail/json_text.hpp>
#include <scrydantic/error.hpp>
#include <scrydantic/json.hpp>
#include <string>
#include <string_view>
#include <utility>

namespace scrydantic {
namespace {

// JsonView stores its node type-erased, as a shared_ptr<const void> aliasing the
// parsed document, so the public header stays free of the document's types; every
// accessor recovers the real node through here.
[[nodiscard]] const detail::json::Value* node(const void* value) noexcept {
  return static_cast<const detail::json::Value*>(value);
}

} // namespace

JsonView::JsonView(std::shared_ptr<const void> value) noexcept
    : value_(std::move(value)) {}

Result<JsonView> JsonView::parse(const std::string_view text) {
  auto document = detail::json::parse(text);
  if (!document) {
    // A fixed phrase and a position in the input: nothing a model may not see.
    auto message =
        "JSON text is not valid: " + detail::json::describe(text, document.error());
    return std::unexpected(
        Error{.message = message, .model_message = std::move(message)});
  }
  return JsonView{std::shared_ptr<const void>{
      std::make_shared<const detail::json::Value>(std::move(*document))}};
}

Result<JsonView> JsonView::parse(const Json& json) {
  return parse(std::string_view{json.text});
}

JsonKind JsonView::kind() const noexcept {
  const auto* value = node(value_.get());
  return value == nullptr ? JsonKind::null : value->kind();
}

std::optional<bool> JsonView::boolean() const noexcept {
  const auto* value = node(value_.get());
  return value == nullptr ? std::nullopt : value->boolean();
}

std::optional<std::int64_t> JsonView::signed_integer() const noexcept {
  const auto* value = node(value_.get());
  return value == nullptr ? std::nullopt : value->signed_integer();
}

std::optional<std::uint64_t> JsonView::unsigned_integer() const noexcept {
  const auto* value = node(value_.get());
  return value == nullptr ? std::nullopt : value->unsigned_integer();
}

std::optional<double> JsonView::number() const noexcept {
  const auto* value = node(value_.get());
  return value == nullptr ? std::nullopt : value->number();
}

std::optional<std::string_view> JsonView::string() const noexcept {
  const auto* value = node(value_.get());
  return value == nullptr ? std::nullopt : value->string();
}

std::size_t JsonView::size() const noexcept {
  const auto* value = node(value_.get());
  return value == nullptr ? 0 : value->size();
}

std::optional<JsonView> JsonView::at(const std::size_t index) const noexcept {
  const auto* value = node(value_.get());
  const auto* elements = value == nullptr ? nullptr : value->array();
  if (elements == nullptr || index >= elements->size()) {
    return std::nullopt;
  }
  return JsonView{std::shared_ptr<const void>{value_, &(*elements)[index]}};
}

std::optional<std::string_view>
JsonView::key_at(const std::size_t index) const noexcept {
  const auto* value = node(value_.get());
  const auto* members = value == nullptr ? nullptr : value->object();
  if (members == nullptr || index >= members->size()) {
    return std::nullopt;
  }
  return std::string_view{(*members)[index].first};
}

std::optional<JsonView> JsonView::find(const std::string_view name) const noexcept {
  const auto* value = node(value_.get());
  const auto* found = value == nullptr ? nullptr : value->find(name);
  if (found == nullptr) {
    return std::nullopt;
  }
  return JsonView{std::shared_ptr<const void>{value_, found}};
}

Json JsonView::to_json() const {
  const auto* value = node(value_.get());
  if (value == nullptr) {
    return Json{.text = "null"};
  }
  return Json{.text = detail::json::write(*value)};
}

std::string escape_json_string(const std::string_view value) {
  return detail::json::quote(value);
}

namespace detail {
namespace {

// What a default-constructed view shows.
const json::Value null_value{};

} // namespace

json_node::json_node(const JsonView& view) noexcept
    : value_{view.value_ ? view.value_.get() : &null_value} {}

JsonKind json_node::kind() const noexcept { return node(value_)->kind(); }

std::optional<bool> json_node::boolean() const noexcept {
  return node(value_)->boolean();
}

std::optional<std::int64_t> json_node::signed_integer() const noexcept {
  return node(value_)->signed_integer();
}

std::optional<std::uint64_t> json_node::unsigned_integer() const noexcept {
  return node(value_)->unsigned_integer();
}

std::optional<double> json_node::number() const noexcept {
  return node(value_)->number();
}

std::optional<std::string_view> json_node::string() const noexcept {
  return node(value_)->string();
}

std::size_t json_node::size() const noexcept { return node(value_)->size(); }

json_node json_node::find(const std::string_view name) const noexcept {
  return json_node{node(value_)->find(name)};
}

Json json_node::to_json() const { return Json{.text = json::write(*node(value_))}; }

json_node json_node::at(const std::size_t index) const noexcept {
  return json_node{&(*node(value_)->array())[index]};
}

std::string_view json_node::key_at(const std::size_t index) const noexcept {
  return (*node(value_)->object())[index].first;
}

} // namespace detail

} // namespace scrydantic
