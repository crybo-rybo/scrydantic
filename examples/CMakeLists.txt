# Publishes reflected tools as an MCP-style listing and serves calls, using only
# the installed package's headers. It reports through its exit code, so it runs
# under ctest when the tests are built.
add_executable(scrydantic_mcp_tools_example mcp_tools.cpp)
target_link_libraries(
  scrydantic_mcp_tools_example
  PRIVATE scrydantic::scrydantic scrydantic_project_options
)
if(SCRYDANTIC_BUILD_TESTS)
  add_test(
    NAME scrydantic.example.mcp-tools
    COMMAND scrydantic_mcp_tools_example
  )
  set_tests_properties(
    scrydantic.example.mcp-tools
    PROPERTIES LABELS example
  )
endif()
