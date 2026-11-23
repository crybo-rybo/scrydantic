#include <scrydantic/tools.hpp>
#include <string>

struct Session {
  std::string user;
};

namespace greeting_tools {
// The context is the host's to supply, so no schema describes it.
[[= scrydantic::tool{"Greet someone"}]] inline std::string
    greet([[= scrydantic::description{"Who is calling"}]] const Session& session,
          std::string greeting) {
  return greeting + session.user;
}
} // namespace greeting_tools

auto bind_tools() { return scrydantic::bind<^^greeting_tools, Session>(); }
