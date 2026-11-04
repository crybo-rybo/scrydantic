#include <cstdint>
#include <scrydantic/codec.hpp>

// A lower bound is inclusive or exclusive, not both.
struct Score {
  [[= scrydantic::range{.min = 0, .exclusive_min = 0}]] std::int32_t points{};
};

static_assert(!scrydantic::schema_v<Score>.empty());
