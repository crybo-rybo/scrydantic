#include <scrydantic/codec.hpp>
#include <string>
#include <variant>

// A variant inside a variant has the kinds of all its alternatives, which the
// outer variant can list itself.
struct Reply {
  std::variant<std::string, std::variant<int, bool>> result;
};

static_assert(!scrydantic::schema_v<Reply>.empty());
