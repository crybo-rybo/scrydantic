#include <optional>
#include <scrydantic/codec.hpp>
#include <string>

// Only an aggregate has fields to give. An optional one would give them all or
// none, which no schema of the enclosing object says.
struct Page {
  std::optional<std::string> next_cursor;
};

struct Results {
  [[= scrydantic::flatten]] std::optional<Page> page;
};

static_assert(!scrydantic::schema_v<Results>.empty());
