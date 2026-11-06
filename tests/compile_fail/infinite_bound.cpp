#include <limits>
#include <scrydantic/codec.hpp>

constexpr double infinity = std::numeric_limits<double>::infinity();

// JSON cannot spell an infinite bound, and decoding admits no infinite value.
struct Reading {
  [[= scrydantic::range{.max = infinity}]] double value{};
};

static_assert(!scrydantic::schema_v<Reading>.empty());
