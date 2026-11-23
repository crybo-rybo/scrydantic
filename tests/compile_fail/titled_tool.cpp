#include <cstdint>
#include <scrydantic/tools.hpp>

namespace volume_tools {
// A title on the function would title nothing: a tool has no schema of its own
// but its arguments', and their class or members carry a title instead.
[[ = scrydantic::tool{"Set the volume"},
   = scrydantic::title{"Volume"} ]] inline std::int32_t set_volume(std::int32_t level) {
  return level;
}
} // namespace volume_tools

auto bind_tools() { return scrydantic::bind<^^volume_tools>(); }
