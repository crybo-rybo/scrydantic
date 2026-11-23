#include <map>
#include <scrydantic/codec.hpp>

// A JSON object's keys are strings, so a map keyed by anything else has no
// JSON form that reads back as the same keys.
struct Names {
  std::map<int, bool> by_id;
};

static_assert(!scrydantic::schema_v<Names>.empty());
