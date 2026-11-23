#include <scrydantic/codec.hpp>

// kebab_case puts every letter in lower case, so both enumerators would be the
// string "low", and one string would stand for two values.
enum class[[= scrydantic::rename_all{scrydantic::kebab_case}]] Level {
  Low,
  low,
};

scrydantic::Result<scrydantic::Json> encode_level(const Level level) {
  return scrydantic::encode(level);
}
