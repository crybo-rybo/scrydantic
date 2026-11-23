#include <optional>
#include <scrydantic/codec.hpp>
#include <string>

// flatten says where one member's fields go; a class is flattened by the
// member that holds it, not by itself.
struct[[= scrydantic::flatten]] Page {
  std::optional<std::string> next_cursor;
};

static_assert(!scrydantic::schema_v<Page>.empty());
