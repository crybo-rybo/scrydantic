#include <scrydantic/codec.hpp>
#include <vector>

// Only a reflected object has a data member behind each key; a vector of
// objects has elements, and each of those has the members.
struct Point {
  int x{};
  int y{};
};

static_assert(scrydantic::field_members_v<std::vector<Point>>.size() == 2);
