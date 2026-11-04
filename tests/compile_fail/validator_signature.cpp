#include <cstdint>
#include <scrydantic/codec.hpp>

namespace checks {
// A validator takes the value by const reference, so one signature serves every
// member type, a string as cheaply as an integer.
inline scrydantic::Status positive(const std::int32_t value) {
  return value > 0 ? scrydantic::Status{}
                   : std::unexpected(scrydantic::tool_error("must be positive"));
}
} // namespace checks

struct Order {
  [[= scrydantic::check<&checks::positive>]] std::int32_t quantity{};
};

static_assert(!scrydantic::schema_v<Order>.empty());
