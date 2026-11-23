#include <map>
#include <scrydantic/codec.hpp>
#include <string>
#include <variant>

// A map takes any object, a tagged one too, so no tag tells it from an
// aggregate.
struct[[= scrydantic::tag{"circle"}]] Circle {
  double radius{};
};

struct Drawing {
  std::variant<Circle, std::map<std::string, double>> shape;
};

static_assert(!scrydantic::schema_v<Drawing>.empty());
