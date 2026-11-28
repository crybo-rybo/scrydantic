#include <scrydantic/scrydantic.hpp>

struct[[= scrydantic::transparent]] Empty {};

constexpr auto schema = scrydantic::schema_v<Empty>;
