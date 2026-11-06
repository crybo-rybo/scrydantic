#include <cstdint>
#include <scrydantic/codec.hpp>

// JSON Schema's multipleOf must be greater than zero.
struct Grid {
  [[= scrydantic::multiple_of{0}]] std::int32_t step{};
};

static_assert(!scrydantic::schema_v<Grid>.empty());
