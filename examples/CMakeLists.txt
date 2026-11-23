# Lists reflected tools and serves calls to them by name, using only the
# installed package's headers. It reports through its exit code, so it runs
# under ctest when the tests are built.
add_executable(scrydantic_tool_host_example tool_host.cpp)
target_link_libraries(
  scrydantic_tool_host_example
  PRIVATE scrydantic::scrydantic scrydantic_project_options
)
if(SCRYDANTIC_BUILD_TESTS)
  add_test(
    NAME scrydantic.example.tool-host
    COMMAND scrydantic_tool_host_example
  )
  set_tests_properties(
    scrydantic.example.tool-host
    PROPERTIES LABELS example
  )
endif()
