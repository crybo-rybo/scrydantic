#include <scrydantic/codec.hpp>

// enumerators_v lists the names of an enum's enumerators. A class that holds an
// enum has keys, and its member's type has the enumerators.
enum class Color { red, green };

struct Palette {
  Color accent{};
};

static_assert(scrydantic::enumerators_v<Palette>.size() == 2);
