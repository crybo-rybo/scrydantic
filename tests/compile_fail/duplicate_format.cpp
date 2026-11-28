#include <scrydantic/scrydantic.hpp>
#include <string>

struct Event {
  [[
    = scrydantic::format{"date"},
    = scrydantic::keyword_string{"format", "date-time"}
  ]] std::string when;
};

constexpr auto schema = scrydantic::schema_v<Event>;
