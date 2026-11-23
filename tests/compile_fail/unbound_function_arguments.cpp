#include <scrydantic/tools.hpp>

namespace catalog {
// An argument synthesized from a parameter is named after it, and this one has
// no name.
inline int weigh(int) { return 0; }
} // namespace catalog

auto weigh_crate() {
  return scrydantic::apply<^^catalog::weigh>(
      scrydantic::arguments_t<^^catalog::weigh>{});
}
