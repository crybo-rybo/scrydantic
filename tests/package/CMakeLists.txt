# A consumer of the installed package alone, built by CI's package job (and by
# hand: configure with CMAKE_PREFIX_PATH set to the installation prefix). It is
# not part of the main build.
cmake_minimum_required(VERSION 3.30)
project(scrydantic_package_consumer LANGUAGES CXX)

find_package(scrydantic 0.1 CONFIG REQUIRED)

add_executable(scrydantic_package_consumer main.cpp)
target_link_libraries(scrydantic_package_consumer PRIVATE scrydantic::scrydantic)
