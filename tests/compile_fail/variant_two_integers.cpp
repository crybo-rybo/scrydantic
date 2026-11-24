#include <cstdint>
#include <scrydantic/codec.hpp>
#include <variant>

// Two integer types are both a JSON integer. Only an integer and a
// floating-point alternative share the numbers.
struct Counter {
  std::variant<std::int16_t, std::int32_t> value;
};

scrydantic::Result<scrydantic::Json> encode_counter(const Counter& counter) {
  return scrydantic::encode(counter);
}
