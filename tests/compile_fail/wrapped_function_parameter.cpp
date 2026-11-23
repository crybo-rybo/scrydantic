#include <meta>
#include <scrydantic/diagnostics.hpp>
#include <scrydantic/tools.hpp>
#include <string_view>

namespace catalog {
// Not a tool: a library of its own binds it, and reports under its own name.
inline int weigh(int* crate) { return *crate; }
} // namespace catalog

namespace routes {

// A wrapper rejects a function as Scrydantic does, with one error.
template <std::meta::info Function,
          typename Binding = typename[:scrydantic::binding_of(Function, ^^void):]>
int add() {
  constexpr std::string_view problem =
      scrydantic::arguments_diagnostic<Function>("routes::add");
  static_assert(problem.empty(), problem);
  if constexpr (!problem.empty()) {
    return 0;
  } else {
    return scrydantic::apply<Function, void, Binding>(
        scrydantic::arguments_t<Function, void, Binding>{});
  }
}

} // namespace routes

int add_route() { return routes::add<^^catalog::weigh>(); }
