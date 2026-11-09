# Tests are labelled by what they cover, so `ctest -L <label>` runs one group:
# json (the C++23 core), reflection (codec, schemas, tool binding), fuzz (the
# corpus replay), multi-tu, compile-fail, and example.

include(FetchContent)
FetchContent_Declare(
  Catch2
  GIT_REPOSITORY https://github.com/catchorg/Catch2.git
  GIT_TAG 317ac1ed4c0bb6e6b91eafc817e05c488feffcb3
  SYSTEM
  EXCLUDE_FROM_ALL
)
FetchContent_MakeAvailable(Catch2)
foreach(SCRYDANTIC_CATCH_TARGET IN ITEMS Catch2 Catch2WithMain)
  target_compile_features("${SCRYDANTIC_CATCH_TARGET}" PRIVATE cxx_std_23)
endforeach()
list(APPEND CMAKE_MODULE_PATH "${catch2_SOURCE_DIR}/extras")
include(Catch)

# Every public header must compile on its own, so each one becomes a lone
# translation unit that includes nothing else, on an object target of its own
# per library: the JSON core's headers as C++23, the rest as C++26.
file(MAKE_DIRECTORY "${PROJECT_BINARY_DIR}/generated/header_checks")
function(scrydantic_add_header_checks TARGET LIBRARY)
  add_library("${TARGET}" OBJECT)
  # Keep one translation unit per header even in a unity build.
  set_target_properties("${TARGET}" PROPERTIES UNITY_BUILD OFF)
  target_link_libraries("${TARGET}" PRIVATE "${LIBRARY}" scrydantic_project_options)
  foreach(SCRYDANTIC_HEADER IN LISTS ARGN)
    string(REGEX REPLACE "^include/" "" SCRYDANTIC_HEADER_INCLUDE "${SCRYDANTIC_HEADER}")
    string(MAKE_C_IDENTIFIER "${SCRYDANTIC_HEADER_INCLUDE}" SCRYDANTIC_HEADER_ID)
    set(
      SCRYDANTIC_HEADER_CHECK_SOURCE
      "${PROJECT_BINARY_DIR}/generated/header_checks/${SCRYDANTIC_HEADER_ID}.cpp"
    )
    file(
      GENERATE
      OUTPUT "${SCRYDANTIC_HEADER_CHECK_SOURCE}"
      CONTENT "#include <${SCRYDANTIC_HEADER_INCLUDE}>\n"
    )
    target_sources("${TARGET}" PRIVATE "${SCRYDANTIC_HEADER_CHECK_SOURCE}")
  endforeach()
endfunction()

scrydantic_add_header_checks(
  scrydantic_json_header_checks scrydantic::json ${SCRYDANTIC_JSON_PUBLIC_HEADERS}
)
# The JSON core's headers stay C++23 without reflection, as its sources are.
set_target_properties(
  scrydantic_json_header_checks
  PROPERTIES CXX_STANDARD 23 CXX_STANDARD_REQUIRED ON CXX_EXTENSIONS OFF
)
target_compile_options(scrydantic_json_header_checks PRIVATE -Werror=c++26-extensions)
scrydantic_add_header_checks(
  scrydantic_header_checks scrydantic::scrydantic ${SCRYDANTIC_PUBLIC_HEADERS}
)

# The JSON core's own tests, compiled as C++23 like the core itself. They reach
# the document type behind JsonView through the private src/ headers. The
# version test is here too, because version.hpp is one of the core's headers.
add_executable(
  scrydantic_json_tests
  json/json_entry_point_tests.cpp
  json/json_document_tests.cpp
  json/json_error_tests.cpp
  json/json_fixture_tests.cpp
  json/json_rejection_tests.cpp
  json/json_view_tests.cpp
  version/version_tests.cpp
)
target_compile_features(scrydantic_json_tests PRIVATE cxx_std_23)
target_include_directories(scrydantic_json_tests PRIVATE "${PROJECT_SOURCE_DIR}/src")
# The JSON golden fixtures are frozen, generated text, checked in as one archive
# (json/json_fixture_tests.cpp describes them) and unpacked at configure time. The corpora supply
# the prefixes the truncation replay walks.
set(
  SCRYDANTIC_JSON_FIXTURE_ARCHIVE
  "${CMAKE_CURRENT_SOURCE_DIR}/fixtures/json/goldens.tar.xz"
)
set(SCRYDANTIC_JSON_FIXTURE_DIR "${PROJECT_BINARY_DIR}/tests/fixtures/json")
file(
  ARCHIVE_EXTRACT
  INPUT "${SCRYDANTIC_JSON_FIXTURE_ARCHIVE}"
  DESTINATION "${SCRYDANTIC_JSON_FIXTURE_DIR}"
)
set_property(
  DIRECTORY
  APPEND
  PROPERTY CMAKE_CONFIGURE_DEPENDS "${SCRYDANTIC_JSON_FIXTURE_ARCHIVE}"
)
target_compile_definitions(
  scrydantic_json_tests
  PRIVATE
    "SCRYDANTIC_JSON_FIXTURE_DIR=\"${SCRYDANTIC_JSON_FIXTURE_DIR}\""
    "SCRYDANTIC_FUZZ_CORPUS_DIR=\"${CMAKE_CURRENT_SOURCE_DIR}/fuzz/corpus\""
    "SCRYDANTIC_EXPECTED_VERSION=\"${PROJECT_VERSION}\""
)
target_link_libraries(
  scrydantic_json_tests
  PRIVATE Catch2::Catch2WithMain scrydantic::json scrydantic_project_options
)
catch_discover_tests(
  scrydantic_json_tests
  TEST_PREFIX "scrydantic.json."
  PROPERTIES LABELS json
)

# The JSON fuzz target is a libFuzzer-style LLVMFuzzerTestOneInput entry point
# over the checked-in seed corpus under fuzz/corpus/json. fuzz/replay_main.cpp
# runs every seed through it once, so the corpus is an ordinary regression test
# that a sanitizer build also runs under ASan and UBSan.
add_executable(
  scrydantic_json_fuzz_replay
  fuzz/replay_main.cpp
  fuzz/json_fuzz.cpp
)
target_compile_features(scrydantic_json_fuzz_replay PRIVATE cxx_std_23)
target_include_directories(
  scrydantic_json_fuzz_replay
  PRIVATE "${PROJECT_SOURCE_DIR}/src"
)
target_link_libraries(
  scrydantic_json_fuzz_replay
  PRIVATE scrydantic::json scrydantic_project_options
)
add_test(
  NAME scrydantic.json-fuzz-replay
  COMMAND scrydantic_json_fuzz_replay "${CMAKE_CURRENT_SOURCE_DIR}/fuzz/corpus/json"
)
set_tests_properties(
  scrydantic.json-fuzz-replay
  PROPERTIES TIMEOUT 45 LABELS "json;fuzz"
)

# The reflected codec, schemas, and tool binding, linked against Scrydantic and
# Catch2 alone.
add_executable(
  scrydantic_tests
  codec/codec_boundary_tests.cpp
  codec/codec_tests.cpp
  codec/constraint_tests.cpp
  codec/number_spelling_tests.cpp
  codec/validator_tests.cpp
  tools/tool_binding_tests.cpp
)
target_link_libraries(
  scrydantic_tests
  PRIVATE Catch2::Catch2WithMain scrydantic::scrydantic scrydantic_project_options
)
catch_discover_tests(
  scrydantic_tests
  TEST_PREFIX "scrydantic."
  PROPERTIES LABELS reflection
)

# Benchmarks of the JSON core, the codec, and bound tool calls. They build with
# the tests when enabled but are not registered with CTest: their numbers mean
# something only from an optimized build (benchmarks/benchmarks.cpp says how).
if(SCRYDANTIC_BUILD_BENCHMARKS)
  add_executable(scrydantic_benchmarks benchmarks/benchmarks.cpp)
  target_link_libraries(
    scrydantic_benchmarks
    PRIVATE Catch2::Catch2WithMain scrydantic::scrydantic scrydantic_project_options
  )
endif()

# A binding must generate code from the declarations its own translation unit
# sees (a namespace's tools, a function's parameter names and annotations),
# however the linker orders the units, so the same program is linked in both
# orders.
foreach(SCRYDANTIC_LINK_ORDER IN ITEMS a_then_b b_then_a)
  if(SCRYDANTIC_LINK_ORDER STREQUAL "a_then_b")
    set(SCRYDANTIC_MULTI_TU_UNITS tools_a.cpp tools_b.cpp)
  else()
    set(SCRYDANTIC_MULTI_TU_UNITS tools_b.cpp tools_a.cpp)
  endif()
  list(TRANSFORM SCRYDANTIC_MULTI_TU_UNITS PREPEND "multi_tu/")
  add_executable(
    scrydantic_multi_tu_${SCRYDANTIC_LINK_ORDER}
    multi_tu/main.cpp
    ${SCRYDANTIC_MULTI_TU_UNITS}
  )
  target_link_libraries(
    scrydantic_multi_tu_${SCRYDANTIC_LINK_ORDER}
    PRIVATE scrydantic::scrydantic scrydantic_project_options
  )
  add_test(
    NAME "scrydantic.multi-tu-${SCRYDANTIC_LINK_ORDER}"
    COMMAND scrydantic_multi_tu_${SCRYDANTIC_LINK_ORDER}
  )
  set_tests_properties(
    "scrydantic.multi-tu-${SCRYDANTIC_LINK_ORDER}"
    PROPERTIES LABELS "reflection;multi-tu"
  )
endforeach()

# Each rejection names the type, member, function, or namespace at fault and the
# reason, with only Scrydantic's headers on the include path.
function(
  scrydantic_add_compile_fail_test
  TEST_NAME
  SOURCE_NAME
  EXPECTED_SUBSTRING
)
  add_test(
    NAME "scrydantic.compile-fail.${TEST_NAME}"
    COMMAND
      "${CMAKE_COMMAND}"
      "-DCXX_COMPILER=${CMAKE_CXX_COMPILER}"
      "-DSOURCE_FILE=${CMAKE_CURRENT_SOURCE_DIR}/compile_fail/${SOURCE_NAME}"
      "-DPROJECT_INCLUDE_DIR=${PROJECT_SOURCE_DIR}/include"
      "-DEXPECTED_SUBSTRING=${EXPECTED_SUBSTRING}"
      -P "${PROJECT_SOURCE_DIR}/cmake/ExpectCompileFailure.cmake"
  )
  set_tests_properties(
    "scrydantic.compile-fail.${TEST_NAME}"
    PROPERTIES LABELS compile-fail
  )
endfunction()

scrydantic_add_compile_fail_test(
  duplicate-description
  duplicate_description.cpp
  "Arguments::value: a reflected member may have at most one scrydantic::description annotation"
)
scrydantic_add_compile_fail_test(
  unsupported-schema-root
  unsupported_schema_root.cpp
  "does not satisfy scrydantic::SupportedValue: scrydantic::Json is raw JSON, which has no schema"
)
scrydantic_add_compile_fail_test(
  untagged-variant
  untagged_variant.cpp
  "Arguments::shape: std::variant<Circle, Square> has the alternative Circle without a scrydantic::tag annotation"
)
scrydantic_add_compile_fail_test(
  duplicate-key
  duplicate_key.cpp
  "does not satisfy scrydantic::Encodable: Limits has members limit and max_tokens that both map to the JSON key"
)
scrydantic_add_compile_fail_test(
  endless-recursion
  endless_recursion.cpp
  "Node::next: Node contains itself through no std::optional, std::vector, or variant alternative after the first, so default-constructing it, as decoding does, never ends"
)
scrydantic_add_compile_fail_test(
  definition-name-collision
  definition_name_collision.cpp
  "Tallies has the recursive types Tally<long long int> and Tally<long int>, which both map to the schema definition name \"Tally-int64\""
)
scrydantic_add_compile_fail_test(
  local-definition
  local_definition.cpp
  "outline_schema()::Report has the recursive type outline_schema()::Outline, which no schema definition name can spell: outline_schema()::Outline is local to a function; declare it in a namespace"
)
scrydantic_add_compile_fail_test(
  non-ascii-definition
  non_ascii_definition.cpp
  "Sizes has the recursive type Größe, which no schema definition name can spell: Größe has a name that is not ASCII letters, digits, and '_'"
)
scrydantic_add_compile_fail_test(
  unspelled-definition-argument
  unspelled_definition_argument.cpp
  "Pointed has the recursive type Tagged<int*>, which no schema definition name can spell: int* is not a type that a definition name spells"
)
scrydantic_add_compile_fail_test(
  tool-parameter-definitions
  tool_parameter_definitions.cpp
  "tally_tools::compare has, among its parameters, the recursive types Tally<long long int> and Tally<long int>, which both map to the schema definition name \"Tally-int64\""
)
scrydantic_add_compile_fail_test(
  bind-misplaced-tool
  bind_misplaced_tool.cpp
  "scrydantic::bind<^^lamp_tools>(): lamp_tools::brightness carries scrydantic::tool, which applies to functions"
)
scrydantic_add_compile_fail_test(
  bind-member-function
  bind_member_function.cpp
  "scrydantic::bind<^^Lamp::switch_on>(): Lamp::switch_on is a non-static member function; bind an object of Lamp as a toolbox"
)
scrydantic_add_compile_fail_test(
  misplaced-context
  misplaced_context.cpp
  "greeting_tools::greet has parameter `session` of type Session; the context is only ever the leading parameter, as const Session&"
)
scrydantic_add_compile_fail_test(
  misapplied-constraint
  misapplied_constraint.cpp
  "Profile::age: carries scrydantic::length, which applies to std::string members, not to int"
)
scrydantic_add_compile_fail_test(
  fixed-size-items
  fixed_size_items.cpp
  "Grid::cells: carries scrydantic::items, which does not apply to std::array<bool, 3>, whose size is fixed"
)
scrydantic_add_compile_fail_test(
  crossed-range
  crossed_range.cpp
  "does not satisfy scrydantic::Decodable: Volume::level: carries a scrydantic::range whose min, 10, exceeds its max, 1"
)
scrydantic_add_compile_fail_test(
  empty-range
  empty_range.cpp
  "Slot::index: carries a scrydantic::range that no value of int satisfies"
)
scrydantic_add_compile_fail_test(
  bound-outside-type
  bound_outside_type.cpp
  "does not satisfy scrydantic::Encodable: Counter::count: carries a scrydantic::range whose min, -1, is outside the range of unsigned int"
)
scrydantic_add_compile_fail_test(
  fractional-bound
  fractional_bound.cpp
  "Rating::stars: carries a scrydantic::range whose max, 1.5, is not an integer"
)
scrydantic_add_compile_fail_test(
  inexact-float-bound
  inexact_float_bound.cpp
  "Gain::value: carries a scrydantic::range whose max, 0.1, is not exactly a float"
)
scrydantic_add_compile_fail_test(
  infinite-bound
  infinite_bound.cpp
  "Reading::value: carries a scrydantic::range whose max, infinity, is not finite"
)
scrydantic_add_compile_fail_test(
  unbounded-range
  unbounded_range.cpp
  "Score::points: carries a scrydantic::range that sets no bound"
)
scrydantic_add_compile_fail_test(
  two-lower-bounds
  two_lower_bounds.cpp
  "Score::points: carries a scrydantic::range that sets both min and exclusive_min"
)
scrydantic_add_compile_fail_test(
  nonpositive-multiple
  nonpositive_multiple.cpp
  "Grid::step: carries a scrydantic::multiple_of whose value, 0, is not positive"
)
scrydantic_add_compile_fail_test(
  decimal-multiple
  decimal_multiple.cpp
  "Price::amount: carries a scrydantic::multiple_of whose value, 0.1, is not exactly that decimal in binary"
)
scrydantic_add_compile_fail_test(
  empty-multiple
  empty_multiple.cpp
  "Grid::step: carries a scrydantic::multiple_of that sets no value"
)
scrydantic_add_compile_fail_test(
  crossed-length
  crossed_length.cpp
  "Account::password: carries a scrydantic::length whose min, 8, exceeds its max, 4"
)
scrydantic_add_compile_fail_test(
  unbounded-items
  unbounded_items.cpp
  "Post::tags: carries a scrydantic::items that sets no bound"
)
scrydantic_add_compile_fail_test(
  duplicate-constraint
  duplicate_constraint.cpp
  "Volume::level: has more than one scrydantic::range annotation"
)
scrydantic_add_compile_fail_test(
  constrained-class
  constrained_class.cpp
  "does not satisfy scrydantic::SupportedValue: Point carries scrydantic::range, which applies to data members"
)
scrydantic_add_compile_fail_test(
  constrained-tool
  constrained_tool.cpp
  "scrydantic::bind<^^volume_tools>(): volume_tools::set_volume carries scrydantic::range, which applies to data members"
)
scrydantic_add_compile_fail_test(
  validator-signature
  validator_signature.cpp
  "Order::quantity: carries scrydantic::check<&checks::positive>, whose validator is std::expected<void, scrydantic::Error>(int); a validator here takes const int& and returns scrydantic::Status"
)
scrydantic_add_compile_fail_test(
  validator-not-function
  validator_not_function.cpp
  "Order::quantity: carries scrydantic::check<42>, whose argument is not a pointer to a function"
)
scrydantic_add_compile_fail_test(
  null-validator
  null_validator.cpp
  "does not satisfy scrydantic::Decodable: Order::quantity: carries a scrydantic::check of a null function pointer"
)
scrydantic_add_compile_fail_test(
  duplicate-validator
  duplicate_validator.cpp
  "Order::quantity: carries scrydantic::check<&checks::positive> twice"
)
scrydantic_add_compile_fail_test(
  class-validator-signature
  class_validator_signature.cpp
  "Range carries scrydantic::check<&checks::ordered>, whose validator is std::expected<void, scrydantic::Error>(const int&); a validator here takes const Range& and returns scrydantic::Status"
)
scrydantic_add_compile_fail_test(
  checked-tool
  checked_tool.cpp
  "scrydantic::bind<^^order_tools>(): order_tools::order carries scrydantic::check, which applies to data members and classes"
)
scrydantic_add_compile_fail_test(
  annotated-parameter
  annotated_parameter.cpp
  "scrydantic::bind<^^volume_tools>(): volume_tools::set_volume has parameter `level` carrying a Scrydantic annotation, which a parameter cannot hold; take an argument aggregate and annotate its member"
)
scrydantic_add_compile_fail_test(
  keys-of-non-object
  keys_of_non_object.cpp
  "std::variant<Circle> is not a plain aggregate, so it has no JSON object keys"
)
scrydantic_add_compile_fail_test(
  tags-of-non-variant
  tags_of_non_variant.cpp
  "Circle is not a std::variant, so it has no tags"
)
scrydantic_add_compile_fail_test(
  keys-of-endless-recursion
  keys_of_endless_recursion.cpp
  "does not satisfy scrydantic::Encodable: Node::next: Node contains itself through no std::optional, std::vector, or variant, so every value of it is infinite or cyclic and cannot be JSON"
)
