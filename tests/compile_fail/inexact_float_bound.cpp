#include <scrydantic/codec.hpp>

// The double 0.1 is not a float: a float member decodes "0.1" as 0.1f, which is
// above it, so the check would reject the schema's own maximum. 0.1f is the bound.
struct Gain {
  [[= scrydantic::range{.max = 0.1}]] float value{};
};

static_assert(!scrydantic::schema_v<Gain>.empty());
