// The version macros agree with the project version that CMake read from the
// same header, so a consumer's preprocessor checks see the released version.

#include <catch2/catch_test_macros.hpp>
#include <scrydantic/version.hpp>
#include <string_view>

static_assert(SCRYDANTIC_VERSION == (SCRYDANTIC_VERSION_MAJOR * 10000) +
                                        (SCRYDANTIC_VERSION_MINOR * 100) +
                                        SCRYDANTIC_VERSION_PATCH);
static_assert(std::string_view{SCRYDANTIC_VERSION_STRING} ==
              SCRYDANTIC_EXPECTED_VERSION);

TEST_CASE("the version macros spell the project version") {
  CHECK(std::string_view{SCRYDANTIC_VERSION_STRING} == SCRYDANTIC_EXPECTED_VERSION);
}
