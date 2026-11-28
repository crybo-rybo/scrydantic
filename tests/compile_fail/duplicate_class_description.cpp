#include <scrydantic/scrydantic.hpp>

struct[[= scrydantic::description{"First"}]][[= scrydantic::description{"Second"}]]
    Address {
  int zip{};
};

constexpr auto schema = scrydantic::schema_v<Address>;
