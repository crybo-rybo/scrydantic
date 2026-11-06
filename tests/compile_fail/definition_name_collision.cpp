#include <scrydantic/codec.hpp>
#include <vector>

// long and long long are distinct types of one width, so Tally<long> and
// Tally<long long> spell one definition name, and a schema cannot define both.
template <typename Integer> struct Tally {
  std::vector<Tally> parts;
  Integer count{};
};

struct Tallies {
  Tally<long> wide;
  Tally<long long> wider;
};

static_assert(!scrydantic::schema_v<Tallies>.empty());
