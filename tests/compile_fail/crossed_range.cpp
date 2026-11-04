#include <cstdint>
#include <scrydantic/codec.hpp>

// A minimum above the maximum admits nothing.
struct Volume {
  [[= scrydantic::range{.min = 10, .max = 1}]] std::int32_t level{};
};

scrydantic::Result<Volume> decode_volume(const scrydantic::Json& json) {
  return scrydantic::decode<Volume>(json);
}
