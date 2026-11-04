#include <cstdint>
#include <scrydantic/codec.hpp>

// An unsigned member holds no -1, so the bound would mean nothing to the check.
struct Counter {
  [[= scrydantic::range{.min = -1}]] std::uint32_t count{};
};

scrydantic::Result<scrydantic::Json> encode_counter(const Counter& counter) {
  return scrydantic::encode(counter);
}
