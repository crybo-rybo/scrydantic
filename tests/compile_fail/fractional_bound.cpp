#include <cstdint>
#include <scrydantic/codec.hpp>

// An integer member is never 1.5, so the schema's maximum and the check's would
// differ.
struct Rating {
  [[= scrydantic::range{.max = 1.5}]] std::int32_t stars{};
};

static_assert(!scrydantic::schema_v<Rating>.empty());
