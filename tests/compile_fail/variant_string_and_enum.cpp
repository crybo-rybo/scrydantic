#include <scrydantic/codec.hpp>
#include <string>
#include <variant>

// A std::string and an enum are both a JSON string, so no string could say
// which of the two alternatives it is.
enum class Mode { fast, slow };

struct Setting {
  std::variant<std::string, Mode> mode;
};

static_assert(!scrydantic::schema_v<Setting>.empty());
