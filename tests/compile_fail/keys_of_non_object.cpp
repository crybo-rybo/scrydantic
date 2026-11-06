#include <scrydantic/codec.hpp>
#include <variant>

// Only a reflected object has JSON keys; a variant's alternatives each have their own.
struct[[= scrydantic::tag{"circle"}]] Circle {
  double radius{};
};

static_assert(scrydantic::keys_v<std::variant<Circle>>.size() == 1);
