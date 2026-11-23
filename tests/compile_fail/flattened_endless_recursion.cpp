#include <memory>
#include <scrydantic/codec.hpp>

// A flattened member is built with the object that holds it, so a Link that
// owns another through one has no end, as it would through any other member.
struct Link;

struct Next {
  std::indirect<Link> link;
};

struct Link {
  int value{};
  [[= scrydantic::flatten]] Next next;
};

static_assert(!scrydantic::schema_v<Link>.empty());
