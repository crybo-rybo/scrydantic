#include <scrydantic/codec.hpp>
#include <string_view>

// A rule names what an enum declares. On one enumerator it would name nothing;
// scrydantic::name is the only annotation an enumerator takes.
enum class Level {
  low_water,
  high_water[[= scrydantic::rename_all{scrydantic::camel_case}]],
};

struct Gauge {
  Level level{};
};

std::string_view gauge_schema() { return scrydantic::schema_v<Gauge>; }
