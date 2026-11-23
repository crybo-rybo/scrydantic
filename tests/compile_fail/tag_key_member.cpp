#include <scrydantic/codec.hpp>
#include <variant>

// The key belongs to the alternatives, which each name it; a member that holds
// the variant has no tag to put under it.
struct[[= scrydantic::tag{"circle"}]] Circle {
  double radius{};
};

struct Drawing {
  [[= scrydantic::tag_key{"kind"}]] std::variant<Circle> shape;
};

static_assert(!scrydantic::schema_v<Drawing>.empty());
