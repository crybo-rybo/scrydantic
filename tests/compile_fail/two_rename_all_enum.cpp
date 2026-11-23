#include <scrydantic/codec.hpp>
#include <string_view>

// An enumerator is one string in JSON, so an enum states one rule.
enum class[[
  = scrydantic::rename_all{scrydantic::camel_case},
  = scrydantic::rename_all{scrydantic::kebab_case}
]] Level {
  low_water,
  high_water,
};

struct Gauge {
  Level level{};
};

scrydantic::Result<Gauge> decode_gauge(const std::string_view text) {
  return scrydantic::decode<Gauge>(text);
}
