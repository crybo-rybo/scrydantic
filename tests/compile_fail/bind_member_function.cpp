#include <scrydantic/tools.hpp>

// A non-static member function needs an object to be called on, which only a
// toolbox binding supplies.
struct Lamp {
  [[= scrydantic::tool{"Switch the lamp on"}]] bool switch_on() { return true; }
};

auto bind_tools() { return scrydantic::bind<^^Lamp::switch_on>(); }
