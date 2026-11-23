#include <optional>
#include <scrydantic/codec.hpp>
#include <string>

// A flattened member is no member of the JSON object, so a description of it
// would appear in no schema.
struct Page {
  std::optional<std::string> next_cursor;
};

struct Results {
  [[ = scrydantic::description{"Where to resume"}, = scrydantic::flatten ]] Page page;
};

static_assert(!scrydantic::schema_v<Results>.empty());
