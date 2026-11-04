#include <array>
#include <scrydantic/codec.hpp>

// A std::array's size is already its schema's minItems and maxItems, so an items
// constraint could only repeat it or contradict it.
struct Grid {
  [[= scrydantic::items{.max = 4}]] std::array<bool, 3> cells{};
};

static_assert(!scrydantic::schema_v<Grid>.empty());
