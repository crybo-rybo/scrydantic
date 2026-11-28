#include <scrydantic/scrydantic.hpp>

struct Order {
  [[= scrydantic::validator]] int quantity{};
};

auto result = scrydantic::decode<Order>("{}");
