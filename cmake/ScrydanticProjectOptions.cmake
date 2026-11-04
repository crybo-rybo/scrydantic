# scrydantic_project_options: the warning, hardening, sanitizer, and coverage
# flags of Scrydantic's own targets (the JSON core, tests, examples, and
# benchmarks). It is never installed or exported, so no consumer sees them.

add_library(scrydantic_project_options INTERFACE)

# High-signal warnings that GCC reports without false positives on this code,
# reflection and splices included. -Wuseless-cast and -Wduplicated-branches are
# left out on purpose: both misfire on generic code, where a cast or a branch
# is redundant for some instantiations only.
target_compile_options(
  scrydantic_project_options
  INTERFACE
    -Wall
    -Wextra
    -Wpedantic
    -Wconversion
    -Wsign-conversion
    -Wdouble-promotion
    -Wshadow
    -Wold-style-cast
    -Wcast-qual
    -Wextra-semi
    -Wnon-virtual-dtor
    -Woverloaded-virtual
    -Wnull-dereference
    -Wformat=2
    -Wimplicit-fallthrough
    -Wduplicated-cond
    -Wlogical-op
    -Wundef
)
if(SCRYDANTIC_WARNINGS_AS_ERRORS)
  target_compile_options(scrydantic_project_options INTERFACE -Werror)
endif()

# Debug builds check libstdc++'s preconditions (an index in range, an engaged
# optional, a non-empty container's front), which changes no ABI.
target_compile_definitions(
  scrydantic_project_options
  INTERFACE "$<$<CONFIG:Debug>:_GLIBCXX_ASSERTIONS>"
)

if(SCRYDANTIC_SANITIZER STREQUAL "address-undefined")
  set(
    SCRYDANTIC_SANITIZER_FLAGS
    -fsanitize=address,undefined
    -fno-sanitize-recover=undefined
  )
elseif(SCRYDANTIC_SANITIZER STREQUAL "thread")
  set(SCRYDANTIC_SANITIZER_FLAGS -fsanitize=thread)
elseif(NOT SCRYDANTIC_SANITIZER STREQUAL "none")
  message(FATAL_ERROR "Unsupported SCRYDANTIC_SANITIZER: ${SCRYDANTIC_SANITIZER}")
endif()
if(SCRYDANTIC_SANITIZER_FLAGS)
  target_compile_options(
    scrydantic_project_options
    INTERFACE ${SCRYDANTIC_SANITIZER_FLAGS} -fno-omit-frame-pointer
  )
  target_link_options(
    scrydantic_project_options
    INTERFACE ${SCRYDANTIC_SANITIZER_FLAGS}
  )
endif()

# gcov instrumentation for scripts/coverage.sh. Header-only code is counted in
# every test that instantiates it; consteval code runs in the compiler and is
# not counted at all.
if(SCRYDANTIC_COVERAGE)
  target_compile_options(scrydantic_project_options INTERFACE --coverage)
  target_link_options(scrydantic_project_options INTERFACE --coverage)
endif()
