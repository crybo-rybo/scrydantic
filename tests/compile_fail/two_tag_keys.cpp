#include <scrydantic/codec.hpp>
#include <string>

// A tag is written under one key.
struct[[
  = scrydantic::tag{"form"}, = scrydantic::tag_key{"mode"},
  = scrydantic::tag_key{"kind"}
]] FormRequest {
  std::string message;
};

static_assert(!scrydantic::schema_v<FormRequest>.empty());
