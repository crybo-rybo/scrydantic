#include <map>
#include <scrydantic/codec.hpp>
#include <string>

// No constraint bounds a map: items counts the elements of a std::vector, and
// here it would be silently ignored rather than bound the number of entries.
struct Labels {
  [[= scrydantic::items{.max = 4}]] std::map<std::string, std::string> values;
};

static_assert(!scrydantic::schema_v<Labels>.empty());
