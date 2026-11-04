#include <scrydantic/codec.hpp>
#include <string>
#include <vector>

// The defaults of an items constraint bound nothing.
struct Post {
  [[= scrydantic::items{}]] std::vector<std::string> tags;
};

static_assert(!scrydantic::schema_v<Post>.empty());
