#include <scrydantic/codec.hpp>
#include <string>
#include <string_view>
#include <variant>

// A trailing underscore is dropped, so `type_` is the key "type", which is
// where a variant alternative carries its tag.
struct[[ = scrydantic::tag{"image"}, = scrydantic::rename_all{scrydantic::camel_case} ]]
    Image {
  std::string type_;
};

struct Post {
  std::variant<Image> attachment;
};

std::string_view post_schema() { return scrydantic::schema_v<Post>; }
