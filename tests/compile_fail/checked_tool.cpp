#include <cstdint>
#include <scrydantic/tools.hpp>

namespace checks {
inline scrydantic::Status positive(const std::int32_t& value) {
  return value > 0 ? scrydantic::Status{}
                   : std::unexpected(scrydantic::tool_error("must be positive"));
}
} // namespace checks

namespace order_tools {
// A function's arguments are checked through their members; a check on the
// function itself would have no value to take.
[[
  = scrydantic::tool{"Order a quantity"}, = scrydantic::check<&checks::positive>
]] inline std::int32_t order(std::int32_t quantity) {
  return quantity;
}
} // namespace order_tools

auto bind_tools() { return scrydantic::bind<^^order_tools>(); }
