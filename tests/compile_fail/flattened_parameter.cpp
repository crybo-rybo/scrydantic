#include <cstdint>
#include <scrydantic/tools.hpp>
#include <string>

struct Page {
  std::int32_t size{};
};

namespace catalog {
// A parameter is one argument under one key; an argument aggregate can flatten
// a member of its own.
[[= scrydantic::tool{"Search the catalog"}]] inline std::string
    search(std::string query, [[= scrydantic::flatten]] Page page) {
  return query + std::to_string(page.size);
}
} // namespace catalog

auto bind_tools() { return scrydantic::bind<^^catalog>(); }
