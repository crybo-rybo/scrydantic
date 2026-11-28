#include <scrydantic/scrydantic.hpp>

namespace port_tools {
[[ = scrydantic::tool{"Open a port"}, = scrydantic::transparent ]] void open() {}
} // namespace port_tools

auto bound = scrydantic::bind<^^port_tools>();
