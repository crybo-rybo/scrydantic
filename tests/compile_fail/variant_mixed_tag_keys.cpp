#include <scrydantic/codec.hpp>
#include <string>
#include <variant>

// A variant reads its tag under one key, so its tagged alternatives name the
// same one: here the first names "mode" and the second, naming none, "type".
struct[[ = scrydantic::tag{"form"}, = scrydantic::tag_key{"mode"} ]] FormRequest {
  std::string message;
};

struct[[= scrydantic::tag{"url"}]] UrlRequest {
  std::string url;
};

struct Dialog {
  std::variant<FormRequest, UrlRequest> request;
};

static_assert(!scrydantic::schema_v<Dialog>.empty());
