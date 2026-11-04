#include <cstdint>
#include <scrydantic/codec.hpp>

// A range that sets no bound constrains nothing, which is never what it meant.
struct Score {
  [[= scrydantic::range{}]] std::int32_t points{};
};

static_assert(!scrydantic::schema_v<Score>.empty());
