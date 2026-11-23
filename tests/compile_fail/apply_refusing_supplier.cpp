#include <expected>
#include <scrydantic/tools.hpp>
#include <string>

struct Admin {
  std::string name{};
};

struct Session {
  std::string user{};
};

template <> struct scrydantic::supplier<Admin, Session> {
  static scrydantic::Result<Admin> supply(const Session& session) {
    if (session.user.empty()) {
      return std::unexpected(scrydantic::tool_error("nobody is signed in"));
    }
    return Admin{.name = session.user};
  }
};

namespace reports {
inline int purge(Admin admin, int count) {
  return count + static_cast<int>(admin.name.size());
}
} // namespace reports

// apply() returns the int that purge returns, which has no place for the
// supplier's refusal: try_apply() returns it.
auto purge_reports(const Session& session) {
  return scrydantic::apply<^^reports::purge, Session>(
      session, scrydantic::arguments_t<^^reports::purge, Session>{});
}
