#include <scrydantic/scrydantic.hpp>

struct Range {
  int low{};
  int high{};

  [[= scrydantic::validator]] bool ordered() const { return low <= high; }
};

auto result = scrydantic::decode<Range>("{}");
