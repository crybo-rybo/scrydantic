# Scrydantic: C++26 reflection to JSON Schema, a strict JSON codec, and
# reflected tools. It builds on its own or as a subdirectory of another project.
# The library depends on nothing but the C++ standard library; scripts/lint.sh
# holds every header and source to that boundary. Only the tests fetch a
# third-party library (Catch2).
cmake_minimum_required(VERSION 3.30)

project(
  scrydantic
  VERSION 0.1.0
  DESCRIPTION "C++26 reflection to JSON Schema, a strict JSON codec, and reflected tools"
  LANGUAGES CXX
)

# Scrydantic uses no C++ modules, so dependency scanning stays off, and its own
# targets compile as standard C++ (-std=c++26, not -std=gnu++26).
set(CMAKE_CXX_SCAN_FOR_MODULES OFF)
set(CMAKE_CXX_EXTENSIONS OFF)

include(CMakePackageConfigHelpers)
include(GNUInstallDirs)

option(SCRYDANTIC_BUILD_TESTS "Build Scrydantic's tests" ${PROJECT_IS_TOP_LEVEL})
option(
  SCRYDANTIC_BUILD_EXAMPLES
  "Compile Scrydantic's examples"
  ${PROJECT_IS_TOP_LEVEL}
)
option(
  SCRYDANTIC_BUILD_BENCHMARKS
  "Build Scrydantic's benchmarks (needs SCRYDANTIC_BUILD_TESTS)"
  OFF
)
option(
  SCRYDANTIC_WARNINGS_AS_ERRORS
  "Treat compiler warnings as errors"
  ${PROJECT_IS_TOP_LEVEL}
)
option(
  SCRYDANTIC_COVERAGE
  "Instrument Scrydantic's own targets for gcov (scripts/coverage.sh)"
  OFF
)
option(SCRYDANTIC_INSTALL "Generate Scrydantic's install rules" ON)
if(SCRYDANTIC_BUILD_BENCHMARKS AND NOT SCRYDANTIC_BUILD_TESTS)
  message(WARNING "SCRYDANTIC_BUILD_BENCHMARKS needs SCRYDANTIC_BUILD_TESTS; no benchmarks will be built")
endif()
set(
  SCRYDANTIC_SANITIZER
  "none"
  CACHE STRING
  "Sanitizer mode: none, address-undefined, or thread"
)
set_property(
  CACHE SCRYDANTIC_SANITIZER
  PROPERTY STRINGS none address-undefined thread
)

# The reflected codec uses C++26 reflection (P2996), which only GCC 16+ compiles.
if(
  NOT CMAKE_CXX_COMPILER_ID STREQUAL "GNU"
  OR CMAKE_CXX_COMPILER_VERSION VERSION_LESS 16
)
  message(
    FATAL_ERROR
    "Scrydantic requires GCC 16 or newer (C++26 reflection), but the C++ "
    "compiler is ${CMAKE_CXX_COMPILER_ID} ${CMAKE_CXX_COMPILER_VERSION} "
    "(${CMAKE_CXX_COMPILER}). Configure a fresh build directory with CXX set "
    "to GCC 16, such as CXX=g++-16, or with -DCMAKE_CXX_COMPILER=g++-16."
  )
endif()

include(cmake/ScrydanticProjectOptions.cmake)

# The JSON core: the strict parser, the canonical writer, and JsonView. It is
# compiled as C++23 without -freflection, so plain clang-tidy can analyze it
# (scripts/lint.sh) and a C++23 consumer that does not enable reflection can
# link it. src/json/core.hpp states its include boundary.
add_library(
  scrydantic_json
  STATIC
    src/json/document.cpp
    src/json/error.cpp
    src/json/number.cpp
    src/json/text.cpp
    src/json/view.cpp
)
add_library(scrydantic::json ALIAS scrydantic_json)
target_compile_features(scrydantic_json PUBLIC cxx_std_23)
# In C++23 mode GCC rejects a reflection operator outright but accepts a P3394
# annotation with only a -Wc++26-extensions warning, so that warning is an error
# for the core even when SCRYDANTIC_WARNINGS_AS_ERRORS is off.
target_compile_options(scrydantic_json PRIVATE -Werror=c++26-extensions)
set_target_properties(
  scrydantic_json
  PROPERTIES EXPORT_NAME json POSITION_INDEPENDENT_CODE ON
)
target_include_directories(
  scrydantic_json
  PUBLIC
    "$<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/include>"
    "$<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>"
  PRIVATE
    "${PROJECT_SOURCE_DIR}/src"
)
target_link_libraries(
  scrydantic_json
  PRIVATE "$<BUILD_INTERFACE:scrydantic_project_options>"
)
set(
  SCRYDANTIC_JSON_PUBLIC_HEADERS
  include/scrydantic/detail/json_text.hpp
  include/scrydantic/error.hpp
  include/scrydantic/json.hpp
)
target_sources(
  scrydantic_json
  PUBLIC
    FILE_SET scrydantic_json_headers
    TYPE HEADERS
    BASE_DIRS "${PROJECT_SOURCE_DIR}/include"
    FILES ${SCRYDANTIC_JSON_PUBLIC_HEADERS}
)

# The reflected codec, schema generator, and tool binding: header-only C++26
# with reflection on top of the JSON core, so its consumers compile as C++26 with
# -freflection too.
add_library(scrydantic INTERFACE)
add_library(scrydantic::scrydantic ALIAS scrydantic)
set_target_properties(scrydantic PROPERTIES EXPORT_NAME scrydantic)
target_compile_features(scrydantic INTERFACE cxx_std_26)
target_compile_options(scrydantic INTERFACE -freflection)
target_link_libraries(scrydantic INTERFACE scrydantic::json)

set(
  SCRYDANTIC_PUBLIC_HEADERS
  include/scrydantic/annotations.hpp
  include/scrydantic/codec.hpp
  include/scrydantic/detail/binding.hpp
  include/scrydantic/detail/constraints.hpp
  include/scrydantic/detail/decode.hpp
  include/scrydantic/detail/encode.hpp
  include/scrydantic/detail/failure.hpp
  include/scrydantic/detail/json_number.hpp
  include/scrydantic/detail/json_string.hpp
  include/scrydantic/detail/meta.hpp
  include/scrydantic/detail/model.hpp
  include/scrydantic/detail/schema.hpp
  include/scrydantic/detail/tools.hpp
  include/scrydantic/scrydantic.hpp
  include/scrydantic/tools.hpp
)
target_sources(
  scrydantic
  INTERFACE
    FILE_SET scrydantic_headers
    TYPE HEADERS
    BASE_DIRS "${PROJECT_SOURCE_DIR}/include"
    FILES ${SCRYDANTIC_PUBLIC_HEADERS}
)

# CTest is enabled here when Scrydantic is the top-level project; a parent
# project that builds Scrydantic's tests enables CTest itself.
if(SCRYDANTIC_BUILD_TESTS)
  if(PROJECT_IS_TOP_LEVEL)
    include(CTest)
  endif()
  add_subdirectory(tests)
endif()

if(SCRYDANTIC_BUILD_EXAMPLES)
  add_subdirectory(examples)
endif()

if(NOT SCRYDANTIC_INSTALL)
  return()
endif()

configure_package_config_file(
  cmake/scrydanticConfig.cmake.in
  "${PROJECT_BINARY_DIR}/scrydanticConfig.cmake"
  INSTALL_DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/scrydantic"
)
# Before 1.0, a minor release may break the API.
write_basic_package_version_file(
  "${PROJECT_BINARY_DIR}/scrydanticConfigVersion.cmake"
  VERSION "${PROJECT_VERSION}"
  COMPATIBILITY SameMinorVersion
)

install(
  TARGETS scrydantic_json
  EXPORT scrydanticTargets
  FILE_SET scrydantic_json_headers
    DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}"
)
install(
  TARGETS scrydantic
  EXPORT scrydanticTargets
  FILE_SET scrydantic_headers
    DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}"
)
install(
  EXPORT scrydanticTargets
  FILE scrydanticTargets.cmake
  NAMESPACE scrydantic::
  DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/scrydantic"
)
install(
  FILES
    "${PROJECT_BINARY_DIR}/scrydanticConfig.cmake"
    "${PROJECT_BINARY_DIR}/scrydanticConfigVersion.cmake"
  DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/scrydantic"
)

install(
  FILES LICENSE
  DESTINATION "${CMAKE_INSTALL_DATAROOTDIR}/doc/scrydantic"
)
