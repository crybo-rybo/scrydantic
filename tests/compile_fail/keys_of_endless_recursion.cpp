#include <memory>
#include <scrydantic/codec.hpp>

// keys_v checks a type as encode() does, so a cycle that no value can end fails
// with the reason that encode() gives.
struct Node {
  int value{};
  std::indirect<Node> next;
};

static_assert(scrydantic::keys_v<Node>.size() == 2);
