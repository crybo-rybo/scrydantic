#include <memory>
#include <scrydantic/codec.hpp>

// fields_v checks a type as keys_v does, which is as encode() does, so a cycle
// that no value can end fails with the reason that encode() gives.
struct Node {
  int value{};
  std::indirect<Node> next;
};

static_assert(scrydantic::fields_v<Node>.size() == 2);
