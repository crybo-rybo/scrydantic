#include <scrydantic/tools.hpp>

struct Lamp {
  int level{};
  int dim(int by) { return level -= by; }
};

// A non-static member function has an argument object, but no object to call
// it on here.
auto dim_lamp() {
  return scrydantic::apply<^^Lamp::dim>(scrydantic::arguments_t<^^Lamp::dim>{.by = 1});
}
