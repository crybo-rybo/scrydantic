#include <scrydantic/codec.hpp>

// A Json is any JSON value, whose schema is empty: no constraint bounds it.
struct Envelope {
  [[= scrydantic::length{.max = 64}]] scrydantic::Json payload;
};

static_assert(!scrydantic::schema_v<Envelope>.empty());
