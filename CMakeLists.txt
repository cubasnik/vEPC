cmake_minimum_required(VERSION 3.10)
project(vEPC)
set(CMAKE_CXX_STANDARD 17)

# Корень проекта в include path (для tests/ → #include "src/...")
include_directories(${CMAKE_SOURCE_DIR})

enable_testing()

# Главная программа vEPC
add_executable(vepc
    main.cpp
    src/diameter_parser.cpp
    src/gtp_parser.cpp
    src/s1ap_parser.cpp
)

# CLI для управления vEPC
add_executable(vepc-cli
    cli/vepc-cli.cpp
    cli/cli_server.h
)

if(UNIX)
    find_library(READLINE_LIB readline)
    find_path(READLINE_INCLUDE readline/readline.h)
    if(READLINE_LIB AND READLINE_INCLUDE)
        message(STATUS "Readline found: ${READLINE_LIB} — enabling Tab completion")
        target_compile_definitions(vepc-cli PRIVATE VEPC_USE_READLINE)
        target_link_libraries(vepc-cli ${READLINE_LIB})
    else()
        message(WARNING "Readline not found — Tab completion disabled")
    endif()
endif()

add_executable(test-gtp-parser
    tests/test_gtp_parser.cpp
    src/gtp_parser.cpp
)

add_executable(test-s1ap-parser
    tests/test_s1ap_parser.cpp
    src/s1ap_parser.cpp
)

add_executable(test-runtime-cli
    tests/test_runtime_cli.cpp
)

if(UNIX)
add_executable(test-linux-interface
    tests/test_linux_interface.cpp
)
endif()

add_executable(test-s11-gtpc-client
    tests/test_s11_gtpc_client.cpp
    src/gtp_parser.cpp
)

add_executable(test-diameter-parser
    tests/test_diameter_parser.cpp
    src/diameter_parser.cpp
)

add_executable(test-s6a-diameter-client
    tests/test_s6a_diameter_client.cpp
    src/diameter_parser.cpp
)

add_test(NAME gtp-parser-smoke COMMAND test-gtp-parser)
add_test(NAME diameter-parser-smoke COMMAND test-diameter-parser)
add_test(NAME s1ap-parser-smoke COMMAND test-s1ap-parser)
if(UNIX)
add_test(NAME linux-interface-smoke COMMAND test-linux-interface)
endif()
add_test(
    NAME runtime-state-recovery-smoke
    COMMAND ${CMAKE_COMMAND}
        -DVEPC_EXE=$<TARGET_FILE:vepc>
        -DSOURCE_DIR=${CMAKE_SOURCE_DIR}
        -P ${CMAKE_SOURCE_DIR}/cmake/TestRuntimeStateRecovery.cmake
)
add_test(
    NAME runtime-state-restore-smoke
    COMMAND ${CMAKE_COMMAND}
        -DVEPC_EXE=$<TARGET_FILE:vepc>
        -DRUNTIME_CLI_EXE=$<TARGET_FILE:test-runtime-cli>
        -DSOURCE_DIR=${CMAKE_SOURCE_DIR}
        -P ${CMAKE_SOURCE_DIR}/cmake/TestRuntimeStateRestore.cmake
)
add_test(
    NAME gtp-u-telemetry-smoke
    COMMAND ${CMAKE_COMMAND}
        -DVEPC_EXE=$<TARGET_FILE:vepc>
        -DRUNTIME_CLI_EXE=$<TARGET_FILE:test-runtime-cli>
        -DSOURCE_DIR=${CMAKE_SOURCE_DIR}
        -P ${CMAKE_SOURCE_DIR}/cmake/TestGtpuTelemetry.cmake
)
add_test(
    NAME s6a-telemetry-smoke
    COMMAND ${CMAKE_COMMAND}
        -DVEPC_EXE=$<TARGET_FILE:vepc>
        -DRUNTIME_CLI_EXE=$<TARGET_FILE:test-runtime-cli>
        -DS6A_DIAMETER_CLIENT_EXE=$<TARGET_FILE:test-s6a-diameter-client>
        -DSOURCE_DIR=${CMAKE_SOURCE_DIR}
        -P ${CMAKE_SOURCE_DIR}/cmake/TestS6aTelemetry.cmake
)
add_test(
    NAME s6a-watchdog-smoke
    COMMAND ${CMAKE_COMMAND}
        -DVEPC_EXE=$<TARGET_FILE:vepc>
        -DRUNTIME_CLI_EXE=$<TARGET_FILE:test-runtime-cli>
        -DS6A_DIAMETER_CLIENT_EXE=$<TARGET_FILE:test-s6a-diameter-client>
        -DSOURCE_DIR=${CMAKE_SOURCE_DIR}
        -P ${CMAKE_SOURCE_DIR}/cmake/TestS6aWatchdog.cmake
)
add_test(
    NAME s6a-disconnect-smoke
    COMMAND ${CMAKE_COMMAND}
        -DVEPC_EXE=$<TARGET_FILE:vepc>
        -DRUNTIME_CLI_EXE=$<TARGET_FILE:test-runtime-cli>
        -DS6A_DIAMETER_CLIENT_EXE=$<TARGET_FILE:test-s6a-diameter-client>
        -DSOURCE_DIR=${CMAKE_SOURCE_DIR}
        -P ${CMAKE_SOURCE_DIR}/cmake/TestS6aDisconnect.cmake
)
add_test(
    NAME s6a-auth-smoke
    COMMAND ${CMAKE_COMMAND}
        -DVEPC_EXE=$<TARGET_FILE:vepc>
        -DRUNTIME_CLI_EXE=$<TARGET_FILE:test-runtime-cli>
        -DS6A_DIAMETER_CLIENT_EXE=$<TARGET_FILE:test-s6a-diameter-client>
        -DSOURCE_DIR=${CMAKE_SOURCE_DIR}
        -P ${CMAKE_SOURCE_DIR}/cmake/TestS6aAuth.cmake
)
add_test(
    NAME s6a-location-smoke
    COMMAND ${CMAKE_COMMAND}
        -DVEPC_EXE=$<TARGET_FILE:vepc>
        -DRUNTIME_CLI_EXE=$<TARGET_FILE:test-runtime-cli>
        -DS6A_DIAMETER_CLIENT_EXE=$<TARGET_FILE:test-s6a-diameter-client>
        -DSOURCE_DIR=${CMAKE_SOURCE_DIR}
        -P ${CMAKE_SOURCE_DIR}/cmake/TestS6aLocation.cmake
)
add_test(
    NAME s6a-purge-smoke
    COMMAND ${CMAKE_COMMAND}
        -DVEPC_EXE=$<TARGET_FILE:vepc>
        -DRUNTIME_CLI_EXE=$<TARGET_FILE:test-runtime-cli>
        -DS6A_DIAMETER_CLIENT_EXE=$<TARGET_FILE:test-s6a-diameter-client>
        -DSOURCE_DIR=${CMAKE_SOURCE_DIR}
        -P ${CMAKE_SOURCE_DIR}/cmake/TestS6aPurge.cmake
)
add_test(
    NAME s6a-cancel-location-smoke
    COMMAND ${CMAKE_COMMAND}
        -DVEPC_EXE=$<TARGET_FILE:vepc>
        -DRUNTIME_CLI_EXE=$<TARGET_FILE:test-runtime-cli>
        -DS6A_DIAMETER_CLIENT_EXE=$<TARGET_FILE:test-s6a-diameter-client>
        -DSOURCE_DIR=${CMAKE_SOURCE_DIR}
        -P ${CMAKE_SOURCE_DIR}/cmake/TestS6aCancelLocation.cmake
)
add_test(
    NAME s6a-insert-subscriber-data-smoke
    COMMAND ${CMAKE_COMMAND}
        -DVEPC_EXE=$<TARGET_FILE:vepc>
        -DRUNTIME_CLI_EXE=$<TARGET_FILE:test-runtime-cli>
        -DS6A_DIAMETER_CLIENT_EXE=$<TARGET_FILE:test-s6a-diameter-client>
        -DSOURCE_DIR=${CMAKE_SOURCE_DIR}
        -P ${CMAKE_SOURCE_DIR}/cmake/TestS6aInsertSubscriberData.cmake
)
add_test(
    NAME s6a-delete-subscriber-data-smoke
    COMMAND ${CMAKE_COMMAND}
        -DVEPC_EXE=$<TARGET_FILE:vepc>
        -DRUNTIME_CLI_EXE=$<TARGET_FILE:test-runtime-cli>
        -DS6A_DIAMETER_CLIENT_EXE=$<TARGET_FILE:test-s6a-diameter-client>
        -DSOURCE_DIR=${CMAKE_SOURCE_DIR}
        -P ${CMAKE_SOURCE_DIR}/cmake/TestS6aDeleteSubscriberData.cmake
)
add_test(
    NAME s11-telemetry-smoke
    COMMAND ${CMAKE_COMMAND}
        -DVEPC_EXE=$<TARGET_FILE:vepc>
        -DRUNTIME_CLI_EXE=$<TARGET_FILE:test-runtime-cli>
        -DS11_GTPC_CLIENT_EXE=$<TARGET_FILE:test-s11-gtpc-client>
        -DSOURCE_DIR=${CMAKE_SOURCE_DIR}
        -P ${CMAKE_SOURCE_DIR}/cmake/TestS11Telemetry.cmake
)
add_test(
    NAME s11-delete-pdp-smoke
    COMMAND ${CMAKE_COMMAND}
        -DVEPC_EXE=$<TARGET_FILE:vepc>
        -DRUNTIME_CLI_EXE=$<TARGET_FILE:test-runtime-cli>
        -DS11_GTPC_CLIENT_EXE=$<TARGET_FILE:test-s11-gtpc-client>
        -DSOURCE_DIR=${CMAKE_SOURCE_DIR}
        -P ${CMAKE_SOURCE_DIR}/cmake/TestS11DeletePdp.cmake
)
add_test(
    NAME s11-update-pdp-smoke
    COMMAND ${CMAKE_COMMAND}
        -DVEPC_EXE=$<TARGET_FILE:vepc>
        -DRUNTIME_CLI_EXE=$<TARGET_FILE:test-runtime-cli>
        -DS11_GTPC_CLIENT_EXE=$<TARGET_FILE:test-s11-gtpc-client>
        -DSOURCE_DIR=${CMAKE_SOURCE_DIR}
        -P ${CMAKE_SOURCE_DIR}/cmake/TestS11UpdatePdp.cmake
)
add_test(
    NAME s11-activate-pdp-smoke
    COMMAND ${CMAKE_COMMAND}
        -DVEPC_EXE=$<TARGET_FILE:vepc>
        -DRUNTIME_CLI_EXE=$<TARGET_FILE:test-runtime-cli>
        -DS11_GTPC_CLIENT_EXE=$<TARGET_FILE:test-s11-gtpc-client>
        -DSOURCE_DIR=${CMAKE_SOURCE_DIR}
        -P ${CMAKE_SOURCE_DIR}/cmake/TestS11ActivatePdp.cmake
)
add_test(
    NAME s11-pdu-notification-smoke
    COMMAND ${CMAKE_COMMAND}
        -DVEPC_EXE=$<TARGET_FILE:vepc>
        -DRUNTIME_CLI_EXE=$<TARGET_FILE:test-runtime-cli>
        -DS11_GTPC_CLIENT_EXE=$<TARGET_FILE:test-s11-gtpc-client>
        -DSOURCE_DIR=${CMAKE_SOURCE_DIR}
        -P ${CMAKE_SOURCE_DIR}/cmake/TestS11PduNotification.cmake
)
add_test(
    NAME s11-pdu-notification-reject-smoke
    COMMAND ${CMAKE_COMMAND}
        -DVEPC_EXE=$<TARGET_FILE:vepc>
        -DRUNTIME_CLI_EXE=$<TARGET_FILE:test-runtime-cli>
        -DS11_GTPC_CLIENT_EXE=$<TARGET_FILE:test-s11-gtpc-client>
        -DSOURCE_DIR=${CMAKE_SOURCE_DIR}
        -P ${CMAKE_SOURCE_DIR}/cmake/TestS11PduNotificationReject.cmake
)
add_test(
    NAME s11-failure-report-smoke
    COMMAND ${CMAKE_COMMAND}
        -DVEPC_EXE=$<TARGET_FILE:vepc>
        -DRUNTIME_CLI_EXE=$<TARGET_FILE:test-runtime-cli>
        -DS11_GTPC_CLIENT_EXE=$<TARGET_FILE:test-s11-gtpc-client>
        -DSOURCE_DIR=${CMAKE_SOURCE_DIR}
        -P ${CMAKE_SOURCE_DIR}/cmake/TestS11FailureReport.cmake
)
add_test(
    NAME s11-note-ms-present-smoke
    COMMAND ${CMAKE_COMMAND}
        -DVEPC_EXE=$<TARGET_FILE:vepc>
        -DRUNTIME_CLI_EXE=$<TARGET_FILE:test-runtime-cli>
        -DS11_GTPC_CLIENT_EXE=$<TARGET_FILE:test-s11-gtpc-client>
        -DSOURCE_DIR=${CMAKE_SOURCE_DIR}
        -P ${CMAKE_SOURCE_DIR}/cmake/TestS11NoteMsPresent.cmake
)
add_test(
    NAME s11-identification-smoke
    COMMAND ${CMAKE_COMMAND}
        -DVEPC_EXE=$<TARGET_FILE:vepc>
        -DRUNTIME_CLI_EXE=$<TARGET_FILE:test-runtime-cli>
        -DS11_GTPC_CLIENT_EXE=$<TARGET_FILE:test-s11-gtpc-client>
        -DSOURCE_DIR=${CMAKE_SOURCE_DIR}
        -P ${CMAKE_SOURCE_DIR}/cmake/TestS11Identification.cmake
)
add_test(
    NAME s11-echo-smoke
    COMMAND ${CMAKE_COMMAND}
        -DVEPC_EXE=$<TARGET_FILE:vepc>
        -DRUNTIME_CLI_EXE=$<TARGET_FILE:test-runtime-cli>
        -DS11_GTPC_CLIENT_EXE=$<TARGET_FILE:test-s11-gtpc-client>
        -DSOURCE_DIR=${CMAKE_SOURCE_DIR}
        -P ${CMAKE_SOURCE_DIR}/cmake/TestS11Echo.cmake
)

# Для Linux нужно добавить pthread
set(THREADS_PREFER_PTHREAD_FLAG ON)
find_package(Threads REQUIRED)
target_link_libraries(vepc Threads::Threads)
target_link_libraries(vepc-cli Threads::Threads)

if (NOT WIN32)
    find_package(Curses)
    find_path(READLINE_INCLUDE_DIR readline/readline.h)
    find_library(READLINE_LIBRARY NAMES readline)
    if (READLINE_INCLUDE_DIR AND READLINE_LIBRARY)
        target_include_directories(vepc-cli PRIVATE ${READLINE_INCLUDE_DIR})
        target_link_libraries(vepc-cli ${READLINE_LIBRARY})
        if (CURSES_FOUND)
            target_link_libraries(vepc-cli ${CURSES_LIBRARIES})
        endif()
        target_compile_definitions(vepc-cli PRIVATE VEPC_USE_READLINE=1)
    endif()
endif()

# Windows: линковка с Winsock2
if (WIN32)
    target_link_libraries(vepc ws2_32 iphlpapi)
    target_link_libraries(vepc-cli ws2_32)
    target_link_libraries(test-diameter-parser ws2_32)
    target_link_libraries(test-runtime-cli ws2_32)
    target_link_libraries(test-s6a-diameter-client ws2_32)
    target_link_libraries(test-s11-gtpc-client ws2_32)

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