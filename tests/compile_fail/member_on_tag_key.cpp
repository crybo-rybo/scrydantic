#include <scrydantic/codec.hpp>
#include <string>
#include <variant>

// An alternative's object carries the variant's tag under the key that its
// tag_key names, so no member of it may take that key.
struct[[ = scrydantic::tag{"form"}, = scrydantic::tag_key{"mode"} ]] FormRequest {
  std::string message;
  std::string mode;
};

struct Dialog {
  std::variant<FormRequest> request;
};

static_assert(!scrydantic::schema_v<Dialog>.empty());
