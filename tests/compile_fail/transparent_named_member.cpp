#include <scrydantic/scrydantic.hpp>

struct[[= scrydantic::transparent]] Port {
  [[= scrydantic::name{"p"}]] int value{};
};

constexpr auto schema = scrydantic::schema_v<Port>;
