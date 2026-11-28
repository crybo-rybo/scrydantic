#include <scrydantic/scrydantic.hpp>

struct[[ = scrydantic::transparent, = scrydantic::tag{"port"} ]] Port {
  int value{};
};

constexpr auto schema = scrydantic::schema_v<Port>;
