#include <scrydantic/codec.hpp>
#include <variant>

// tags_v[index] is the tag of the alternative at that index, so a variant with
// an alternative that its JSON kind selects, which carries no tag, has no list.
struct[[= scrydantic::tag{"circle"}]] Circle {
  double radius{};
};

static_assert(scrydantic::tags_v<std::variant<int, Circle>>.size() == 1);
