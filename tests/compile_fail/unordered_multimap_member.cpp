#include <scrydantic/codec.hpp>
#include <string>
#include <unordered_map>

// As a std::multimap, it can hold one key twice, which a JSON object cannot.
struct Headers {
  std::unordered_multimap<std::string, int> fields;
};

scrydantic::Result<Headers> decode_headers(const scrydantic::JsonView& view) {
  return scrydantic::decode<Headers>(view);
}
