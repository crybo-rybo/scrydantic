#include <scrydantic/scrydantic.hpp>

struct[[= scrydantic::transparent]] Port {
  int value{};
};

constexpr auto keys = scrydantic::keys_v<Port>;
