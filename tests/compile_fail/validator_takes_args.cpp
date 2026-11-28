#include <scrydantic/scrydantic.hpp>

struct Range {
  int low{};
  int high{};

  [[= scrydantic::validator]] scrydantic::Status ordered(int) const { return {}; }
};

auto result = scrydantic::decode<Range>("{}");
