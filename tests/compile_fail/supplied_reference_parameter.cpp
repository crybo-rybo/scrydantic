#include <scrydantic/tools.hpp>
#include <string>

struct Clock {
  int now{};
};

struct Session {
  int now{};
};

template <> struct scrydantic::supplier<Clock, Session> {
  static Clock supply(const Session& session) { return Clock{.now = session.now}; }
};

namespace clock_tools {
// The supplier returns a value, which a non-const lvalue reference does not
// bind: only a reference that `supply` returns fills this parameter.
[[= scrydantic::tool{"Wind the clock"}]] inline int wind(Clock& clock, int turns) {
  clock.now += turns;
  return clock.now;
}
} // namespace clock_tools

auto bind_tools() { return scrydantic::bind<^^clock_tools, Session>(); }
