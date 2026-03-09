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
	message(FATAL_ERROR "TestS11FailureReport.cmake currently supports Windows only")
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
	COMMAND "${S11_GTPC_CLIENT_EXE}" failure 15
	WORKING_DIRECTORY "${SOURCE_DIR}"
	RESULT_VARIABLE FAILURE_RESULT
	OUTPUT_VARIABLE FAILURE_OUTPUT
	ERROR_VARIABLE FAILURE_ERROR
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

if(NOT FAILURE_RESULT EQUAL 0)
	message(FATAL_ERROR "Failed to complete S11 Failure Report roundtrip: ${FAILURE_OUTPUT} ${FAILURE_ERROR}")
endif()
if(NOT STATE_RESULT EQUAL 0)
	message(FATAL_ERROR "Failed to query state after Failure Report: ${STATE_ERROR}")
endif()

if(NOT STATE_OUTPUT MATCHES "PDP contexts: 1")
	message(FATAL_ERROR "Expected one PDP context after Failure Report. Output: ${STATE_OUTPUT}")
endif()
if(NOT STATE_OUTPUT MATCHES "- TEID: 0x10004321")
	message(FATAL_ERROR "Expected reported PDP TEID after Failure Report. Output: ${STATE_OUTPUT}")
endif()
if(NOT STATE_OUTPUT MATCHES "Message Type: Failure Report Request \\(0x22\\)")
	message(FATAL_ERROR "Expected Failure Report message type after failure report. Output: ${STATE_OUTPUT}")
endif()
if(NOT STATE_OUTPUT MATCHES "APN: alarm")
	message(FATAL_ERROR "Expected reported APN after Failure Report. Output: ${STATE_OUTPUT}")
endif()
if(NOT STATE_OUTPUT MATCHES "PDP Type: 0x46")
	message(FATAL_ERROR "Expected reported PDP type after Failure Report. Output: ${STATE_OUTPUT}")
endif()
if(NOT STATE_OUTPUT MATCHES "GGSN IP: 10\\.23\\.42\\.90")
	message(FATAL_ERROR "Expected reported GGSN IP after Failure Report. Output: ${STATE_OUTPUT}")
endif()

if(NOT EXISTS "${LOG_FILE}")
	message(FATAL_ERROR "Expected log file ${LOG_FILE} to exist")
endif()

file(READ "${LOG_FILE}" LOG_CONTENT)
if(NOT LOG_CONTENT MATCHES "Failure Report request parsed from 127\\.0\\.0\\.1: teid=0x10004321, reported=yes")
	message(FATAL_ERROR "Missing Failure Report request log entry")
endif()
if(NOT LOG_CONTENT MATCHES "Sent Failure Report response to 127\\.0\\.0\\.1: teid=0x10004321")
	message(FATAL_ERROR "Missing Failure Report response log entry")
endif()