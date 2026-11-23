#pragma once

#include <string_view>

namespace scrydantic {

/// One field of a reflected object type, as the codec and the schema see it.
///
/// scrydantic::fields_v lists them. Every view points at static storage, so a
/// descriptor can be kept and copied freely.
struct FieldDescriptor {
  /// The JSON key, as scrydantic::keys_v lists it.
  std::string_view key{};
  /// The text of the member's scrydantic::description, or empty when it has
  /// none.
  std::string_view description{};
  /// Whether the schema lists the key under `required`, which is whether
  /// decoding rejects an object without it.
  bool required{};
  /// The member's own JSON Schema: the text under the key in the `properties`
  /// of the type's schema. Empty for a type that has no schema.
  std::string_view schema{};
};

} // namespace scrydantic
