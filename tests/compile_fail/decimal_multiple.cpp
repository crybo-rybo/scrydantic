#include <scrydantic/codec.hpp>

// The double nearest 0.1 is not one tenth, so 0.3 is a decimal multiple of 0.1
// for the schema but no binary multiple of that double for the decoder.
struct Price {
  [[= scrydantic::multiple_of{0.1}]] double amount{};
};

static_assert(!scrydantic::schema_v<Price>.empty());
