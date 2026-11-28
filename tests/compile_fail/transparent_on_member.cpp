#include <scrydantic/scrydantic.hpp>

struct Server {
  [[= scrydantic::transparent]] int port{};
};

constexpr auto schema = scrydantic::schema_v<Server>;
