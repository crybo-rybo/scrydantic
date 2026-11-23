#include <memory>
#include <scrydantic/codec.hpp>
#include <string>
#include <variant>

// A variant reads an alternative's kind and tag from the alternative itself,
// not from what a std::indirect owns.
struct Item {
  std::string name;
};

struct Slot {
  std::variant<std::string, std::indirect<Item>> content;
};

static_assert(!scrydantic::schema_v<Slot>.empty());
