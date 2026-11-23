#include <scrydantic/codec.hpp>
#include <string>
#include <variant>

// default_tag says which tag an object without one has, so a class without a
// tag has none to give: a variant's only aggregate is a plain object already.
struct[[= scrydantic::default_tag]] FormRequest {
  std::string message;
};

struct Dialog {
  std::variant<std::string, FormRequest> request;
};

static_assert(!scrydantic::schema_v<Dialog>.empty());
