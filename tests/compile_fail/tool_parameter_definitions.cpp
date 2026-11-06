#include <scrydantic/tools.hpp>
#include <vector>

template <typename Integer> struct Tally {
  std::vector<Tally> parts;
  Integer count{};
};

// Each parameter has a schema alone, but the synthesized argument object defines
// both, and their names are one.
namespace tally_tools {
[[= scrydantic::tool{"Compare two tallies"}]] inline bool
    compare(Tally<long> left, Tally<long long> right) {
  return left.count == right.count;
}
} // namespace tally_tools

auto bind_tools() { return scrydantic::bind<^^tally_tools>(); }
