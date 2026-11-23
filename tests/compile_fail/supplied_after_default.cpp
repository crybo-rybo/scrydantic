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
// A call that leaves `limit` to its default passes nothing after it, so the
// host could not supply `clock` in that call.
[[= scrydantic::tool{"List the alarms"}]] inline int
    alarms(std::string name, int limit = 10, Clock clock = {}) {
  return clock.now + limit + static_cast<int>(name.size());
}
} // namespace clock_tools

auto bind_tools() { return scrydantic::bind<^^clock_tools, Session>(); }
