#include <scrydantic/codec.hpp>
#include <string_view>
#include <vector>

// A recursive class local to a function is a definition with no stable name.
std::string_view outline_schema() {
  struct Outline {
    std::vector<Outline> sections;
  };
  struct Report {
    Outline outline;
  };
  return scrydantic::schema_v<Report>;
}
