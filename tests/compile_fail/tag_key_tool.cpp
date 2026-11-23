#include <cstdint>
#include <scrydantic/tools.hpp>

namespace volume_tools {
// A function is no variant alternative, so it has no tag for a key to carry.
[[ = scrydantic::tool{"Set the volume"},
   = scrydantic::default_tag ]] inline std::int32_t set_volume(std::int32_t level) {
  return level;
}
} // namespace volume_tools

auto bind_tools() { return scrydantic::bind<^^volume_tools>(); }
