#include <optional>
#include <scrydantic/tools.hpp>
#include <string>

namespace listing_tools {
// The default argument already lets `tag` be left out, and is what it then is,
// so skip_null, which would make it disengaged instead, could do nothing.
[[= scrydantic::tool{"Tag a listing"}]] inline std::string
    tagged(std::string query,
           [[= scrydantic::skip_null]] std::optional<std::string> tag = std::nullopt) {
  return query + tag.value_or("");
}
} // namespace listing_tools

auto bind_tools() { return scrydantic::bind<^^listing_tools>(); }
