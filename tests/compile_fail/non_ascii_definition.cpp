#include <scrydantic/codec.hpp>
#include <vector>

// A definition name holds only ASCII letters, digits, '_', '.', and '-'.
struct Größe {
  std::vector<Größe> parts;
};

struct Sizes {
  Größe size;
};

static_assert(!scrydantic::schema_v<Sizes>.empty());
