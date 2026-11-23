#include <cstdint>
#include <scrydantic/tools.hpp>

namespace page_tools {
// A function has no fields to give and no object to give them to.
[[ = scrydantic::tool{"Turn the page"},
   = scrydantic::flatten ]] inline std::int32_t turn(std::int32_t page) {
  return page + 1;
}
} // namespace page_tools

auto bind_tools() { return scrydantic::bind<^^page_tools>(); }
