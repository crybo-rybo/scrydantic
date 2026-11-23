#include <scrydantic/codec.hpp>
#include <string>

// A tag_key names the key of the class's tag, so a class without a tag has
// nothing for it to name.
struct[[= scrydantic::tag_key{"mode"}]] FormRequest {
  std::string message;
};

static_assert(!scrydantic::schema_v<FormRequest>.empty());
