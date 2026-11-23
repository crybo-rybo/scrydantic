#include <scrydantic/codec.hpp>
#include <string>

// An empty key names nothing; leave the annotation out for "type".
struct[[ = scrydantic::tag{"form"}, = scrydantic::tag_key{""} ]] FormRequest {
  std::string message;
};

static_assert(!scrydantic::schema_v<FormRequest>.empty());
