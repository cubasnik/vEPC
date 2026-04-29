cmake_minimum_required(VERSION 3.10)

project(MyCppProject)

set(CMAKE_CXX_STANDARD 11)

include_directories(include)

file(GLOB SOURCES "src/*.cpp" "src/modules/*.cpp")
file(GLOB HEADERS "include/modules/*.h")

add_executable(MyCppProject ${SOURCES} ${HEADERS})

enable_testing()
add_subdirectory(tests)