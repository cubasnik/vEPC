cmake_minimum_required(VERSION 3.10)
project(vEPC)
set(CMAKE_CXX_STANDARD 17)

# Главная программа vEPC
add_executable(vepc
    main.cpp
)

# CLI для управления vEPC
add_executable(vepc-cli
    cli/vepc-cli.cpp
    cli/cli_server.h
)

# Для Linux нужно добавить pthread
set(THREADS_PREFER_PTHREAD_FLAG ON)
find_package(Threads REQUIRED)
target_link_libraries(vepc Threads::Threads)
target_link_libraries(vepc-cli Threads::Threads)

# Windows: линковка с Winsock2
if (WIN32)
    target_link_libraries(vepc ws2_32)
    target_link_libraries(vepc-cli ws2_32)

    # Copy Windows binaries to project root for easy run
    add_custom_command(TARGET vepc POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
                $<TARGET_FILE:vepc>
                ${CMAKE_SOURCE_DIR}/vepc.exe
    )
    add_custom_command(TARGET vepc-cli POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
                $<TARGET_FILE:vepc-cli>
                ${CMAKE_SOURCE_DIR}/vepc-cli.exe
    )
endif()