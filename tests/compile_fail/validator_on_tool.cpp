#include <scrydantic/scrydantic.hpp>

namespace order_tools {
[[ = scrydantic::tool{"Place an order"}, = scrydantic::validator ]] void order() {}
} // namespace order_tools

auto bound = scrydantic::bind<^^order_tools>();
