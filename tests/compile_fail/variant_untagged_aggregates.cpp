#include <scrydantic/codec.hpp>
#include <string>
#include <variant>

// Aggregates are all JSON objects, so beside one another each needs a tag,
// whatever other kinds the variant holds.
struct Item {
  std::string name;
};

struct Other {
  bool flag{};
};

struct Basket {
  std::variant<std::string, Item, Other> content;
};

static_assert(!scrydantic::schema_v<Basket>.empty());
