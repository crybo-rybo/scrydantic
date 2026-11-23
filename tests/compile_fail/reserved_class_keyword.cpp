#include <cstdint>
#include <scrydantic/codec.hpp>

// A class's keywords are held to the same list as a member's.
struct[[= scrydantic::keyword{"additionalProperties", "true"}]] Reading {
  std::int32_t level;
};

static_assert(!scrydantic::schema_v<Reading>.empty());
