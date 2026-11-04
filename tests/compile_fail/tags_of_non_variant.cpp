#include <scrydantic/codec.hpp>

// A tag applies inside a variant only, so a lone tagged class has no tags to list.
struct[[= scrydantic::tag{"circle"}]] Circle {
  double radius{};
};

static_assert(scrydantic::tags_v<Circle>.size() == 1);
