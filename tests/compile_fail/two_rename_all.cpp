#include <scrydantic/codec.hpp>
#include <string_view>

// A member has one key, so a class states one rule.
struct[[
  = scrydantic::rename_all{scrydantic::camel_case},
  = scrydantic::rename_all{scrydantic::kebab_case}
]] Limits {
  int max_tokens{};
};

std::string_view limits_schema() { return scrydantic::schema_v<Limits>; }
