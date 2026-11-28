#include <scrydantic/scrydantic.hpp>
#include <vector>

struct[[= scrydantic::transparent]] Loop {
  std::vector<Loop> items;
};

constexpr auto schema = scrydantic::schema_v<Loop>;
