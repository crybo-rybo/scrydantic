#include <scrydantic/tools.hpp>
#include <string>

struct Database {
  int rows{};
};

struct Session {
  const Database* database{};
};

template <> struct scrydantic::supplier<Database, Session> {
  static const Database& supply(const Session& session) { return *session.database; }
};

namespace report_tools {
// The host supplies `db`, so it is no argument and no schema describes it.
[[= scrydantic::tool{"Export the report"}]] inline int
    export_report(std::string name,
                  [[= scrydantic::description{"Where to read"}]] const Database& db) {
  return db.rows + static_cast<int>(name.size());
}
} // namespace report_tools

auto bind_tools() { return scrydantic::bind<^^report_tools, Session>(); }
