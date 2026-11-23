#include <scrydantic/codec.hpp>
#include <variant>

// fields_v lists the fields behind keys_v, so it takes what keys_v takes: a
// variant has no fields of its own, as it has no keys.
struct[[= scrydantic::tag{"circle"}]] Circle {
  double radius{};
};

static_assert(scrydantic::fields_v<std::variant<Circle>>.size() == 1);
