#include <scrydantic/scrydantic.hpp>

struct[[= scrydantic::transparent]] Port {
  int value{};
};

constexpr auto schema = scrydantic::input_schema_v<Port>;
