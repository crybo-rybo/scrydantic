#include <optional>
#include <scrydantic/codec.hpp>
#include <variant>

// An optional's null and a variant's std::monostate are one JSON value: the
// schema would allow null twice, and decoding could only ever read it as the
// empty optional.
struct Reply {
  std::optional<std::variant<std::monostate, int>> result;
};

static_assert(!scrydantic::schema_v<Reply>.empty());
