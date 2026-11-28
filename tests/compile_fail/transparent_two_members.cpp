#include <scrydantic/scrydantic.hpp>

struct[[= scrydantic::transparent]] Pair {
  int first{};
  int second{};
};

constexpr auto schema = scrydantic::schema_v<Pair>;
