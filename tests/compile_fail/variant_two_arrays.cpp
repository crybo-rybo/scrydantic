#include <array>
#include <scrydantic/codec.hpp>
#include <variant>
#include <vector>

// A std::vector and a std::array are both a JSON array, and a variant does not
// try one alternative and then the other.
struct Reading {
  std::variant<std::vector<int>, std::array<int, 2>> values;
};

static_assert(!scrydantic::schema_v<Reading>.empty());
