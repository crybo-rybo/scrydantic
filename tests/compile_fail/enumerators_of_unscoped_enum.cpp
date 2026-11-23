#include <scrydantic/codec.hpp>

// enumerators_v checks an enum as encode() does, so an enum that the codec does
// not read or write has no JSON names to list.
enum Color { red, green };

static_assert(scrydantic::enumerators_v<Color>.size() == 2);
