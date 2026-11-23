#include <cstdint>
#include <scrydantic/tools.hpp>
#include <string>

namespace catalog {
// A parameter's annotations are held to its type as a data member's are.
[[= scrydantic::tool{"Search the catalog"}]] inline std::string
    search(std::string query, [[= scrydantic::length{.max = 9}]] std::int32_t limit) {
  return query + std::to_string(limit);
}
} // namespace catalog

auto bind_tools() { return scrydantic::bind<^^catalog>(); }
