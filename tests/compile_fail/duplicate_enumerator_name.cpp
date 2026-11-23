#include <scrydantic/codec.hpp>

// A name annotation that lands on another enumerator's name would make one
// string stand for two values.
enum class Level {
  low,
  minimum[[= scrydantic::name{"low"}]],
};

scrydantic::Result<scrydantic::Json> encode_level(const Level level) {
  return scrydantic::encode(level);
}
