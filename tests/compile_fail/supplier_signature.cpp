#include <scrydantic/tools.hpp>
#include <string>

struct Database {
  int rows{};
};

struct Session {
  Database* database{};
};

// `supply` is called with the `const Session&` of the call, which this one
// does not take.
template <> struct scrydantic::supplier<Database, Session> {
  static Database& supply(Session& session) { return *session.database; }
};

namespace report_tools {
[[= scrydantic::tool{"Export the report"}]] inline int export_report(std::string name,
                                                                     Database& db) {
  return db.rows + static_cast<int>(name.size());
}
} // namespace report_tools

auto bind_tools() { return scrydantic::bind<^^report_tools, Session>(); }
