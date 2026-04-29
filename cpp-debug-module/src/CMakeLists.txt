set(SOURCES
    main.cpp
    modules/debug_module.cpp
)

add_executable(cpp_debug_module ${SOURCES})

target_include_directories(cpp_debug_module PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../include)