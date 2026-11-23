#include <map>
#include <scrydantic/codec.hpp>
#include <string>

// A multimap can hold one key twice, and a JSON object keeps only one member of
// a key, so encoding one could lose entries.
struct Headers {
  std::multimap<std::string, int> fields;
};

scrydantic::Result<scrydantic::Json> encode_headers(const Headers& headers) {
  return scrydantic::encode(headers);
}
