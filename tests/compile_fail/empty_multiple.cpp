#include <cstdint>
#include <scrydantic/codec.hpp>

// A multiple_of without a factor constrains nothing.
struct Grid {
  [[= scrydantic::multiple_of{}]] std::int32_t step{};
};

static_assert(!scrydantic::schema_v<Grid>.empty());
