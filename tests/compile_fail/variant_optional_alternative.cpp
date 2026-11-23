#include <optional>
#include <scrydantic/codec.hpp>
#include <string>
#include <variant>

// An optional is two JSON kinds, null and its value's. A variant spells null as
// std::monostate.
struct Reply {
  std::variant<std::string, std::optional<int>> result;
};

static_assert(!scrydantic::schema_v<Reply>.empty());
