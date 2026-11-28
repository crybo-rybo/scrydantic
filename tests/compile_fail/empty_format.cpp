#include <scrydantic/scrydantic.hpp>
#include <string>

struct Event {
  [[= scrydantic::format{""}]] std::string when;
};

constexpr auto schema = scrydantic::schema_v<Event>;
