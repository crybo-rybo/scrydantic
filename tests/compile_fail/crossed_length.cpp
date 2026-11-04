#include <scrydantic/codec.hpp>
#include <string>

// No string is at least 8 and at most 4 code points long.
struct Account {
  [[= scrydantic::length{.min = 8, .max = 4}]] std::string password;
};

static_assert(!scrydantic::schema_v<Account>.empty());
