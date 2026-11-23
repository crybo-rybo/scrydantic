#include <cstdint>
#include <scrydantic/tools.hpp>

namespace volume_tools {
// A rule on the function would rename nothing: a tool's arguments are named
// after its parameters, and an argument aggregate states a rule of its own.
[[
  = scrydantic::tool{"Set the volume"}, = scrydantic::rename_all{scrydantic::camel_case}
]] inline std::int32_t set_volume(std::int32_t volume_level) {
  return volume_level;
}
} // namespace volume_tools

auto bind_tools() { return scrydantic::bind<^^volume_tools>(); }
