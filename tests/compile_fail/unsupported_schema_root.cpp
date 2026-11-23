#include <scrydantic/codec.hpp>
#include <string_view>

// Borrowed text is the one encode-only leaf: it is outside the closed
// SupportedValue matrix, so it cannot be a schema root.
static_assert(!scrydantic::schema_v<std::string_view>.empty());
