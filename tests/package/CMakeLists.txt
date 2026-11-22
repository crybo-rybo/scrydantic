# A consumer of Scrydantic, built by CI's package job (and by hand). It is not
# part of the main build. It uses Scrydantic in one of two ways:
#
#   - the installed package: configure with CMAKE_PREFIX_PATH set to the
#     installation prefix;
#   - the source tree, as FetchContent or add_subdirectory() uses it: configure
#     with SCRYDANTIC_SOURCE_DIR set to the repository root.
cmake_minimum_required(VERSION 3.30)
project(scrydantic_package_consumer LANGUAGES CXX)

if(DEFINED SCRYDANTIC_SOURCE_DIR)
  add_subdirectory("${SCRYDANTIC_SOURCE_DIR}" scrydantic)
  get_directory_property(
    SCRYDANTIC_CONSUMED_VERSION
    DIRECTORY "${SCRYDANTIC_SOURCE_DIR}"
    DEFINITION PROJECT_VERSION
  )
else()
  find_package(scrydantic 0.3 CONFIG REQUIRED)
  set(SCRYDANTIC_CONSUMED_VERSION "${scrydantic_VERSION}")
endif()

add_executable(scrydantic_package_consumer main.cpp)
target_link_libraries(scrydantic_package_consumer PRIVATE scrydantic::scrydantic)
# The version macros must spell the version that CMake reports for the package.
target_compile_definitions(
  scrydantic_package_consumer
  PRIVATE "SCRYDANTIC_EXPECTED_VERSION=\"${SCRYDANTIC_CONSUMED_VERSION}\""
)
