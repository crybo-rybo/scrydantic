#include <cstdint>
#include <scrydantic/codec.hpp>

namespace checks {
// A class's validator takes the whole object, not one of its members.
scrydantic::Status ordered(const std::int32_t& low);
} // namespace checks

struct[[= scrydantic::check<&checks::ordered>]] Range {
  std::int32_t low{};
  std::int32_t high{};
};

static_assert(!scrydantic::schema_v<Range>.empty());
