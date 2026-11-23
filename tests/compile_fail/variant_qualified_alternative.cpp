#include <scrydantic/codec.hpp>
#include <string>
#include <variant>

// An alternative is a plain value type, for encoding too, which only reads it.
struct Slot {
  std::variant<std::string, const int> content;
};

scrydantic::Result<scrydantic::Json> encode_slot(const Slot& slot) {
  return scrydantic::encode(slot);
}
