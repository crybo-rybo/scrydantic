#include <cstdint>
#include <scrydantic/codec.hpp>

// A check names a validator function by pointer; any other value is a mistake.
struct Order {
  [[= scrydantic::check<42>]] std::int32_t quantity{};
};

static_assert(!scrydantic::schema_v<Order>.empty());
