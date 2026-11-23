#include <scrydantic/codec.hpp>
#include <string>
#include <variant>

// A Json holds a value of every kind, so beside it no other alternative would
// ever be selected for certain.
struct Envelope {
  std::variant<std::string, scrydantic::Json> payload;
};

static_assert(!scrydantic::schema_v<Envelope>.empty());
