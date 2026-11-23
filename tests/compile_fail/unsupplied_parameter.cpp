#include <scrydantic/tools.hpp>
#include <string>

class Database {
public:
  explicit Database(int rows) : rows_{rows} {}
  [[nodiscard]] int rows() const { return rows_; }

private:
  int rows_;
};

struct Session {
  const Database* database{};
};

namespace report_tools {
// No supplier of Session supplies a Database, and the codec cannot read one.
[[= scrydantic::tool{"Export the report"}]] inline int
    export_report(std::string name, const Database& db) {
  return db.rows() + static_cast<int>(name.size());
}
} // namespace report_tools

auto bind_tools() { return scrydantic::bind<^^report_tools, Session>(); }
