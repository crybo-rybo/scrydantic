#include <cstdint>
#include <scrydantic/codec.hpp>

// A constraint bounds a member's value; a class has members, not one value.
struct[[= scrydantic::range{.min = 0}]] Point {
  std::int32_t x{};
  std::int32_t y{};
};

static_assert(!scrydantic::schema_v<Point>.empty());
