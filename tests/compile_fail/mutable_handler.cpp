#include <cstdint>
#include <scrydantic/tools.hpp>

struct Steps {
  std::int32_t count{};
};

// A handler is called as const, so a lambda that changes its own captures
// cannot be one; the count has to live outside it.
auto bind_counter() {
  return scrydantic::bind_handler<Steps>(
      [walked = std::int32_t{0}](Steps steps) mutable {
        return walked += steps.count;
      });
}
