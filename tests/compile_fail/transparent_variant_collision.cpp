#include <cstdint>
#include <scrydantic/scrydantic.hpp>
#include <variant>

struct[[= scrydantic::transparent]] Port {
  std::uint16_t value{};
};

using Value = std::variant<Port, std::int32_t>;

constexpr auto schema = scrydantic::schema_v<Value>;
