#include <scrydantic/codec.hpp>
#include <string>
#include <variant>

// An object without the tag is read as one alternative, so one carries
// default_tag.
struct[[ = scrydantic::tag{"form"}, = scrydantic::default_tag ]] FormRequest {
  std::string message;
};

struct[[ = scrydantic::tag{"url"}, = scrydantic::default_tag ]] UrlRequest {
  std::string url;
};

struct Dialog {
  std::variant<FormRequest, UrlRequest> request;
};

static_assert(!scrydantic::schema_v<Dialog>.empty());
