#include <scrydantic/scrydantic.hpp>

namespace volume_tools {
[[ = scrydantic::tool{"Set volume"}, = scrydantic::format{"uri"} ]] void set_volume() {}
} // namespace volume_tools

auto bound = scrydantic::bind<^^volume_tools>();
