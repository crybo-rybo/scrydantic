#include <cstdint>
#include <scrydantic/codec.hpp>

// A length counts the code points of a string; an integer has none.
struct Profile {
  [[= scrydantic::length{.max = 3}]] std::int32_t age{};
};

static_assert(!scrydantic::schema_v<Profile>.empty());
