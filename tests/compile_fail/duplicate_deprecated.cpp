#include <scrydantic/scrydantic.hpp>
#include <string>

struct Event {
  [[ = scrydantic::deprecated,
     = scrydantic::keyword{"deprecated", "false"} ]] std::string when;
};

constexpr auto schema = scrydantic::schema_v<Event>;
