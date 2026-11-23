#include <scrydantic/codec.hpp>

// An enumerator has one string in JSON, so it has at most one name.
enum class Level {
  low[[= scrydantic::name{"lo"}]][[= scrydantic::name{"min"}]],
  high,
};

struct Gauge {
  Level level{};
};

scrydantic::Result<Gauge> decode_gauge(const scrydantic::Json& json) {
  return scrydantic::decode<Gauge>(json);
}
