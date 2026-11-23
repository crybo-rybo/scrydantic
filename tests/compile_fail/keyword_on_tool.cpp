#include <cstdint>
#include <scrydantic/tools.hpp>

namespace volume_tools {
// A keyword on the function would join no schema: a tool has none of its own
// but its arguments', and their class or members carry a keyword instead.
[[
  = scrydantic::tool{"Set the volume"},
  = scrydantic::keyword{"deprecated", "true"}
]] inline std::int32_t set_volume(std::int32_t level) {
  return level;
}
} // namespace volume_tools

auto bind_tools() { return scrydantic::bind<^^volume_tools>(); }
