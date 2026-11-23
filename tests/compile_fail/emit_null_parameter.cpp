#include <optional>
#include <scrydantic/tools.hpp>
#include <string>

namespace listing_tools {
// emit_null undoes a class's skip_null for one member. No class holds a
// parameter, so here it would undo nothing.
[[= scrydantic::tool{"Tag a listing"}]] inline std::string
    tagged(std::string query,
           [[= scrydantic::emit_null]] std::optional<std::string> tag) {
  return query + tag.value_or("");
}
} // namespace listing_tools

auto bind_tools() { return scrydantic::bind<^^listing_tools>(); }
