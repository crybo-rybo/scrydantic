#include <scrydantic/codec.hpp>

// camel_case spells both identifiers `maxTokens`, which would write one key
// twice, as a name annotation that lands on another member's key would.
struct[[= scrydantic::rename_all{scrydantic::camel_case}]] Limits {
  int max_tokens{};
  int maxTokens{};
};

scrydantic::Result<scrydantic::Json> encode_limits(const Limits& limits) {
  return scrydantic::encode(limits);
}
