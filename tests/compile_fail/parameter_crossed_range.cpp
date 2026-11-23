#include <cstdint>
#include <scrydantic/tools.hpp>

namespace volume_tools {
std::int32_t
    set_volume([[= scrydantic::range{.min = 10, .max = 1}]] std::int32_t level);
} // namespace volume_tools

// No value meets the range, for a function bound by hand as for a tool.
using Arguments = scrydantic::arguments_t<^^volume_tools::set_volume>;
