#include <scrydantic/codec.hpp>
#include <string>

// A schema has one "title" for a member, so two say nothing definite.
struct Profile {
  [[ = scrydantic::title{"Name"},
     = scrydantic::title{"Display name"} ]] std::string name;
};

static_assert(!scrydantic::schema_v<Profile>.empty());
