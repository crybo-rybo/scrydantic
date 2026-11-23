#include <scrydantic/codec.hpp>
#include <string>

// An empty title labels nothing; leave the annotation out instead.
struct Profile {
  [[= scrydantic::title{""}]] std::string name;
};

static_assert(!scrydantic::schema_v<Profile>.empty());
