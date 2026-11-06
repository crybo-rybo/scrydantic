#include <memory>
#include <scrydantic/codec.hpp>

// Every Node owns another, so default-constructing one, as decoding does, never
// ends, and no finite JSON value matches its schema.
struct Node {
  int value{};
  std::indirect<Node> next;
};

static_assert(!scrydantic::schema_v<Node>.empty());
