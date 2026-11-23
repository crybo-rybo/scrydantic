#include <cstdint>
#include <scrydantic/tools.hpp>

struct Volume {
  std::int32_t level{};
};

namespace volume_tools {
// A lone aggregate parameter is the whole argument object: no member holds it,
// so this description would describe nothing. Its own members carry theirs.
[[= scrydantic::tool{"Set the volume"}]] inline std::int32_t
    set_volume([[= scrydantic::description{"How loud"}]] Volume volume) {
  return volume.level;
}
} // namespace volume_tools

auto bind_tools() { return scrydantic::bind<^^volume_tools>(); }
