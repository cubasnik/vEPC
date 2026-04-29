project(cpp-debug-module)

cmake_minimum_required(VERSION 3.10)

set(CMAKE_CXX_STANDARD 11)

include_directories(include)

file(GLOB SOURCES "src/modules/*.cpp" "src/*.cpp")

add_executable(cpp-debug-module ${SOURCES})

enable_testing()

add_subdirectory(tests)