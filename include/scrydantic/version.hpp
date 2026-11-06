#pragma once

/// Scrydantic's release version, for preprocessor checks such as
/// `#if SCRYDANTIC_VERSION >= 100`. CMakeLists.txt reads the project version
/// from the three component macros, so this file is the one place that sets
/// the version. The header is plain C++23 and declares no other names.

#define SCRYDANTIC_VERSION_MAJOR 0
#define SCRYDANTIC_VERSION_MINOR 2
#define SCRYDANTIC_VERSION_PATCH 0

/// The version as one integer: major * 10000 + minor * 100 + patch.
#define SCRYDANTIC_VERSION                                                             \
  ((SCRYDANTIC_VERSION_MAJOR * 10000) + (SCRYDANTIC_VERSION_MINOR * 100) +             \
   SCRYDANTIC_VERSION_PATCH)

#define SCRYDANTIC_DETAIL_STRINGIFY_IMPL(x) #x
#define SCRYDANTIC_DETAIL_STRINGIFY(x) SCRYDANTIC_DETAIL_STRINGIFY_IMPL(x)

/// The version as a string literal, such as "0.2.0".
#define SCRYDANTIC_VERSION_STRING                                                      \
  SCRYDANTIC_DETAIL_STRINGIFY(SCRYDANTIC_VERSION_MAJOR)                                \
  "." SCRYDANTIC_DETAIL_STRINGIFY(                                                     \
      SCRYDANTIC_VERSION_MINOR) "." SCRYDANTIC_DETAIL_STRINGIFY(SCRYDANTIC_VERSION_PATCH)
