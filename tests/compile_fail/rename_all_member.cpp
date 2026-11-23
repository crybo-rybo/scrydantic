#include <scrydantic/codec.hpp>
#include <string_view>

// A rule names what a class declares. On one member it would name nothing:
// scrydantic::name gives a single member its key.
struct Limits {
  [[= scrydantic::rename_all{scrydantic::camel_case}]] int max_tokens{};
};

std::string_view limits_schema() { return scrydantic::schema_v<Limits>; }
