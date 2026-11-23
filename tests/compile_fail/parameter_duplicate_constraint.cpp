#include <cstdint>
#include <scrydantic/tools.hpp>

namespace volume_tools {
// One range carries both bounds, on a parameter as on a data member.
[[= scrydantic::tool{"Set the volume"}]] inline std::int32_t
    set_volume([[ = scrydantic::range{.min = 0},
                  = scrydantic::range{.max = 11} ]] std::int32_t level) {
  return level;
}
} // namespace volume_tools

auto bind_tools() { return scrydantic::bind<^^volume_tools>(); }
