#include <memory>
#include <scrydantic/tools.hpp>

struct Lamp {
  int level{};

  // The explicit object parameter is the toolbox, not an argument.
  [[= scrydantic::tool{"Dim the lamp"}]] int
      dim([[= scrydantic::name{"lamp"}]] this Lamp& self, int by) {
    return self.level -= by;
  }
};

auto bind_tools() { return scrydantic::bind(std::make_shared<Lamp>()); }
