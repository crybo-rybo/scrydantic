#include <scrydantic/codec.hpp>
#include <string>
#include <variant>

// An alternative's object carries the variant's tag under "type", so no field
// of it may take that key, a flattened member's included.
struct Media {
  std::string type;
};

struct[[= scrydantic::tag{"image"}]] Image {
  [[= scrydantic::flatten]] Media media;
};

struct Post {
  std::variant<Image> attachment;
};

static_assert(!scrydantic::schema_v<Post>.empty());
