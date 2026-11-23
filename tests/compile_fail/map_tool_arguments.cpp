#include <map>
#include <scrydantic/codec.hpp>
#include <string>

// A map is a JSON object, but an argument object's members are the tool's named
// arguments, which a map does not declare. A map is a member of one instead.
static_assert(!scrydantic::input_schema_v<std::map<std::string, int>>.empty());
