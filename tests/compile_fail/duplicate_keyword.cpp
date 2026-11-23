#include <scrydantic/codec.hpp>
#include <string>

// A schema has one value for a key, whichever annotation spells it.
struct Query {
  [[
    = scrydantic::keyword{"format", R"("date")"},
    = scrydantic::keyword_string{"format", "date-time"}
  ]] std::string since;
};

static_assert(!scrydantic::schema_v<Query>.empty());
