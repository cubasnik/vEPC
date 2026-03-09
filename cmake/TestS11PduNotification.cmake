cmake_minimum_required(VERSION 3.10)

if(NOT DEFINED VEPC_EXE)
	message(FATAL_ERROR "VEPC_EXE is required")
endif()

if(NOT DEFINED RUNTIME_CLI_EXE)
	message(FATAL_ERROR "RUNTIME_CLI_EXE is required")
endif()

if(NOT DEFINED S11_GTPC_CLIENT_EXE)
	message(FATAL_ERROR "S11_GTPC_CLIENT_EXE is required")
endif()

if(NOT DEFINED SOURCE_DIR)
	message(FATAL_ERROR "SOURCE_DIR is required")
endif()

if(NOT WIN32)
	message(FATAL_ERROR "TestS11PduNotification.cmake currently supports Windows only")
endif()

set(LOG_DIR "${SOURCE_DIR}/build/logs")
set(LOG_FILE "${LOG_DIR}/vepc.log")
set(STATE_DIR "${SOURCE_DIR}/build/state")
set(STATE_FILE "${STATE_DIR}/runtime_state.json")

file(MAKE_DIRECTORY "${LOG_DIR}")
file(MAKE_DIRECTORY "${STATE_DIR}")
file(REMOVE "${LOG_FILE}")
file(REMOVE "${STATE_FILE}")

set(START_SCRIPT "$proc = Start-Process -FilePath '${VEPC_EXE}' -PassThru
$proc.Id
")

execute_process(
	COMMAND powershell -NoProfile -Command "${START_SCRIPT}"
	WORKING_DIRECTORY "${SOURCE_DIR}"
	RESULT_VARIABLE START_RESULT
	OUTPUT_VARIABLE START_OUTPUT
	ERROR_VARIABLE START_ERROR
	TIMEOUT 20
)
if(NOT START_RESULT EQUAL 0)
	message(FATAL_ERROR "Failed to start vepc.exe: ${START_ERROR}")
endif()

string(STRIP "${START_OUTPUT}" VEPC_PID)
if(NOT VEPC_PID MATCHES "^[0-9]+$")
	message(FATAL_ERROR "Failed to capture vepc PID: '${START_OUTPUT}' '${START_ERROR}'")
endif()

execute_process(
	COMMAND "${S11_GTPC_CLIENT_EXE}" create 15
	WORKING_DIRECTORY "${SOURCE_DIR}"
	RESULT_VARIABLE CREATE_RESULT
	OUTPUT_VARIABLE CREATE_OUTPUT
	ERROR_VARIABLE CREATE_ERROR
	TIMEOUT 20
)
if(NOT CREATE_RESULT EQUAL 0)
	execute_process(COMMAND taskkill /PID ${VEPC_PID} /F OUTPUT_QUIET ERROR_QUIET)
	message(FATAL_ERROR "Failed to complete S11 Create PDP roundtrip: ${CREATE_OUTPUT} ${CREATE_ERROR}")
endif()

execute_process(
	COMMAND "${S11_GTPC_CLIENT_EXE}" notify 15
	WORKING_DIRECTORY "${SOURCE_DIR}"
	RESULT_VARIABLE NOTIFY_RESULT
	OUTPUT_VARIABLE NOTIFY_OUTPUT
	ERROR_VARIABLE NOTIFY_ERROR
	TIMEOUT 20
)

execute_process(
	COMMAND "${RUNTIME_CLI_EXE}" state 20
	WORKING_DIRECTORY "${SOURCE_DIR}"
	RESULT_VARIABLE STATE_RESULT
	OUTPUT_VARIABLE STATE_OUTPUT
	ERROR_VARIABLE STATE_ERROR
	TIMEOUT 20
)

execute_process(
	COMMAND taskkill /PID ${VEPC_PID} /F
	OUTPUT_QUIET
	ERROR_QUIET
)

if(NOT NOTIFY_RESULT EQUAL 0)
	message(FATAL_ERROR "Failed to complete S11 PDU Notification roundtrip: ${NOTIFY_OUTPUT} ${NOTIFY_ERROR}")
endif()
if(NOT STATE_RESULT EQUAL 0)
	message(FATAL_ERROR "Failed to query state after PDU Notification: ${STATE_ERROR}")
endif()

if(NOT STATE_OUTPUT MATCHES "PDP contexts: 1")
	message(FATAL_ERROR "Expected one PDP context after PDU Notification. Output: ${STATE_OUTPUT}")
endif()
if(NOT STATE_OUTPUT MATCHES "- TEID: 0x10004321")
	message(FATAL_ERROR "Expected notified PDP TEID after PDU Notification. Output: ${STATE_OUTPUT}")
endif()
if(NOT STATE_OUTPUT MATCHES "Message Type: PDU Notification Request \\(0x1B\\)")
	message(FATAL_ERROR "Expected PDU Notification message type after notify. Output: ${STATE_OUTPUT}")
endif()
if(NOT STATE_OUTPUT MATCHES "APN: notify")
	message(FATAL_ERROR "Expected notified APN after PDU Notification. Output: ${STATE_OUTPUT}")
endif()
if(NOT STATE_OUTPUT MATCHES "PDP Type: 0x44")
	message(FATAL_ERROR "Expected notified PDP type after PDU Notification. Output: ${STATE_OUTPUT}")
endif()
if(NOT STATE_OUTPUT MATCHES "GGSN IP: 10\.23\.42\.88")
	message(FATAL_ERROR "Expected notified GGSN IP after PDU Notification. Output: ${STATE_OUTPUT}")
endif()

if(NOT EXISTS "${LOG_FILE}")
	message(FATAL_ERROR "Expected log file ${LOG_FILE} to exist")
endif()

file(READ "${LOG_FILE}" LOG_CONTENT)
if(NOT LOG_CONTENT MATCHES "PDU Notification request parsed from 127\.0\.0\.1: teid=0x10004321, notified=yes")
	message(FATAL_ERROR "Missing PDU Notification request log entry")
endif()
if(NOT LOG_CONTENT MATCHES "Sent PDU Notification response to 127\.0\.0\.1: teid=0x10004321")
	message(FATAL_ERROR "Missing PDU Notification response log entry")
endif()