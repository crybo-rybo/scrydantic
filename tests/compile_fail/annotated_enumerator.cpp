#include <scrydantic/codec.hpp>

// Nothing reads a description on an enumerator, so this one would describe
// nothing; scrydantic::name is the only annotation an enumerator takes.
enum class Level {
  low,
  high[[= scrydantic::description{"Above the warning line"}]],
};

struct Gauge {
  Level level{};
};

std::string_view gauge_schema() { return scrydantic::schema_v<Gauge>; }
