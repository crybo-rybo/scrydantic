#include <cstdint>
#include <scrydantic/tools.hpp>

namespace volume_tools {
// A constraint on the function would bound nothing: neither its result, which
// encoding does not check, nor an argument, whose member carries it instead.
[[
  = scrydantic::tool{"Set the volume"}, = scrydantic::range{.max = 11}
]] inline std::int32_t set_volume(std::int32_t level) {
  return level;
}
} // namespace volume_tools

auto bind_tools() { return scrydantic::bind<^^volume_tools>(); }
