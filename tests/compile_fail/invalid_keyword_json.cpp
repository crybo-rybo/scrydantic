#include <scrydantic/codec.hpp>
#include <string>

// A keyword's value is JSON text, and this is a bare word: the string is
// R"("date-time")", or scrydantic::keyword_string{"format", "date-time"}.
struct Query {
  [[= scrydantic::keyword{"format", "date-time"}]] std::string since;
};

static_assert(!scrydantic::schema_v<Query>.empty());
