#include <optional>
#include <scrydantic/codec.hpp>
#include <string>

// A flattened member's fields are fields of the object that holds it, so one of
// them landing on the object's own key would write that key twice.
struct Page {
  std::optional<std::string> next_cursor;
};

struct Results {
  std::string next_cursor;
  [[= scrydantic::flatten]] Page page;
};

scrydantic::Result<scrydantic::Json> encode_results(const Results& results) {
  return scrydantic::encode(results);
}
