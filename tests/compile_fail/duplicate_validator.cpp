#include <cstdint>
#include <scrydantic/codec.hpp>

namespace checks {
inline scrydantic::Status positive(const std::int32_t& value) {
  return value > 0 ? scrydantic::Status{}
                   : std::unexpected(scrydantic::tool_error("must be positive"));
}
} // namespace checks

// The second check could only repeat the first one's verdict.
struct Order {
  [[
    = scrydantic::check<&checks::positive>, = scrydantic::check<&checks::positive>
  ]] std::int32_t quantity{};
};

static_assert(!scrydantic::schema_v<Order>.empty());
