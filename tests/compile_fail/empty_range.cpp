#include <cstdint>
#include <scrydantic/codec.hpp>

// No integer lies strictly between 5 and 6, though the bounds do not cross.
struct Slot {
  [[= scrydantic::range{.exclusive_min = 5, .exclusive_max = 6}]] std::int32_t index{};
};

static_assert(!scrydantic::schema_v<Slot>.empty());
