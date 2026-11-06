#include <cstdint>
#include <scrydantic/tools.hpp>

namespace volume_tools {
// No binding reads a parameter's annotations, so this range would bound
// nothing; a member of an argument aggregate carries it instead.
[[= scrydantic::tool{"Set the volume"}]] inline std::int32_t
    set_volume([[= scrydantic::range{.max = 11}]] std::int32_t level) {
  return level;
}
} // namespace volume_tools

auto bind_tools() { return scrydantic::bind<^^volume_tools>(); }
