#include <scrydantic/codec.hpp>

// validate() reads a value as encode() does, so it takes the Encodable family
// and nothing outside it: a class with a base is no reflected value, whatever
// its members are constrained to.
struct Base {
  int id{};
};

struct Derived : Base {
  [[= scrydantic::range{.min = 0}]] int count{};
};

const scrydantic::Status status = scrydantic::validate(Derived{});
