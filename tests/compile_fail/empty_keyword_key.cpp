#include <cstdint>
#include <scrydantic/codec.hpp>

// A keyword has a name.
struct Reading {
  [[= scrydantic::keyword{"", "1"}]] std::int32_t level;
};

static_assert(!scrydantic::schema_v<Reading>.empty());
