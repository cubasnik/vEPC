cmake_minimum_required(VERSION 3.10)
project(vEPC)
set(CMAKE_CXX_STANDARD 17)

enable_testing()

# Главная программа vEPC
add_executable(vepc
    main.cpp
    src/gtp_parser.cpp
    src/s1ap_parser.cpp
)

# CLI для управления vEPC
add_executable(vepc-cli
    cli/vepc-cli.cpp
    cli/cli_server.h
)

add_executable(test-gtp-parser
    test_gtp_parser.cpp
    src/gtp_parser.cpp
)

add_executable(test-s1ap-parser
    test_s1ap_parser.cpp
    src/s1ap_parser.cpp
)

add_test(NAME gtp-parser-smoke COMMAND test-gtp-parser)
add_test(NAME s1ap-parser-smoke COMMAND test-s1ap-parser)
add_test(
    NAME runtime-state-recovery-smoke
    COMMAND ${CMAKE_COMMAND}
        -DVEPC_EXE=$<TARGET_FILE:vepc>
        -DSOURCE_DIR=${CMAKE_SOURCE_DIR}
        -P ${CMAKE_SOURCE_DIR}/cmake/TestRuntimeStateRecovery.cmake
)

# Для Linux нужно добавить pthread
set(THREADS_PREFER_PTHREAD_FLAG ON)
find_package(Threads REQUIRED)
target_link_libraries(vepc Threads::Threads)
target_link_libraries(vepc-cli Threads::Threads)

# Windows: линковка с Winsock2
if (WIN32)
    target_link_libraries(vepc ws2_32 iphlpapi)
    target_link_libraries(vepc-cli ws2_32)

    set(ROOT_SYNC_SCRIPT ${CMAKE_SOURCE_DIR}/cmake/SyncBinaryToRoot.cmake)

    # Copy Windows binaries to project root for easy run
    add_custom_command(TARGET vepc POST_BUILD
    COMMAND ${CMAKE_COMMAND}
        -DSOURCE=$<TARGET_FILE:vepc>
        -DDESTINATION=${CMAKE_SOURCE_DIR}/vepc.exe
        -DLABEL=vepc.exe
        -P ${ROOT_SYNC_SCRIPT}
    VERBATIM
    )
    add_custom_command(TARGET vepc-cli POST_BUILD
    COMMAND ${CMAKE_COMMAND}
        -DSOURCE=$<TARGET_FILE:vepc-cli>
        -DDESTINATION=${CMAKE_SOURCE_DIR}/vepc-cli.exe
        -DLABEL=vepc-cli.exe
        -P ${ROOT_SYNC_SCRIPT}
    VERBATIM
    )
endif()