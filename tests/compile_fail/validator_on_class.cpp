#include <scrydantic/scrydantic.hpp>

struct[[= scrydantic::validator]] Order {
  int quantity{};
};

auto result = scrydantic::decode<Order>("{}");
