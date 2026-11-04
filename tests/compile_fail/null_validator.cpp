#include <cstdint>
#include <scrydantic/codec.hpp>

using validator = scrydantic::Status (*)(const std::int32_t&);

// Decoding would call through the pointer, so it must point at a function.
struct Order {
  [[= scrydantic::check<validator{}>]] std::int32_t quantity{};
};

scrydantic::Result<Order> decode_order(const scrydantic::Json& json) {
  return scrydantic::decode<Order>(json);
}
