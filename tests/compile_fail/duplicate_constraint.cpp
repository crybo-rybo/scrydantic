#include <cstdint>
#include <scrydantic/codec.hpp>

// One range carries both bounds; two would leave it unclear which bound wins.
struct Volume {
  [[ = scrydantic::range{.min = 0},
     = scrydantic::range{.max = 11} ]] std::int32_t level{};
};

static_assert(!scrydantic::schema_v<Volume>.empty());
