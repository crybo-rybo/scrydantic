#include <cstdint>
#include <scrydantic/codec.hpp>

// "type" is the generator's to write: decoding reads an integer here whatever
// an annotation says.
struct Reading {
  [[= scrydantic::keyword{"type", R"("number")"}]] std::int32_t level;
};

static_assert(!scrydantic::schema_v<Reading>.empty());
