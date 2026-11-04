#include <scrydantic/codec.hpp>
#include <vector>

// A pointer is no template argument that a definition name spells.
template <typename Tag> struct Tagged {
  std::vector<Tagged> children;
};

struct Pointed {
  Tagged<int*> tagged;
};

static_assert(!scrydantic::schema_v<Pointed>.empty());
