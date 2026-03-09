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
	message(FATAL_ERROR "TestS11Identification.cmake currently supports Windows only")
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
	COMMAND "${S11_GTPC_CLIENT_EXE}" identify 15
	WORKING_DIRECTORY "${SOURCE_DIR}"
	RESULT_VARIABLE IDENTIFY_RESULT
	OUTPUT_VARIABLE IDENTIFY_OUTPUT
	ERROR_VARIABLE IDENTIFY_ERROR
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

if(NOT IDENTIFY_RESULT EQUAL 0)
	message(FATAL_ERROR "Failed to complete S11 Identification roundtrip: ${IDENTIFY_OUTPUT} ${IDENTIFY_ERROR}")
endif()
if(NOT STATE_RESULT EQUAL 0)
	message(FATAL_ERROR "Failed to query state after Identification: ${STATE_ERROR}")
endif()

if(NOT STATE_OUTPUT MATCHES "PDP contexts: 1")
	message(FATAL_ERROR "Expected one PDP context after Identification. Output: ${STATE_OUTPUT}")
endif()
if(NOT STATE_OUTPUT MATCHES "- TEID: 0x10004321")
	message(FATAL_ERROR "Expected identified PDP TEID after Identification. Output: ${STATE_OUTPUT}")
endif()
if(NOT STATE_OUTPUT MATCHES "Message Type: Identification Request \\(0x30\\)")
	message(FATAL_ERROR "Expected Identification message type after identify. Output: ${STATE_OUTPUT}")
endif()
if(NOT STATE_OUTPUT MATCHES "APN: ident")
	message(FATAL_ERROR "Expected identification APN after Identification. Output: ${STATE_OUTPUT}")
endif()
if(NOT STATE_OUTPUT MATCHES "PDP Type: 0x48")
	message(FATAL_ERROR "Expected identification PDP type after Identification. Output: ${STATE_OUTPUT}")
endif()
if(NOT STATE_OUTPUT MATCHES "GGSN IP: 10\\.23\\.42\\.92")
	message(FATAL_ERROR "Expected identification GGSN IP after Identification. Output: ${STATE_OUTPUT}")
endif()

if(NOT EXISTS "${LOG_FILE}")
	message(FATAL_ERROR "Expected log file ${LOG_FILE} to exist")
endif()

file(READ "${LOG_FILE}" LOG_CONTENT)
if(NOT LOG_CONTENT MATCHES "Identification request parsed from 127\\.0\\.0\\.1: teid=0x10004321, identified=yes")
	message(FATAL_ERROR "Missing Identification request log entry")
endif()
if(NOT LOG_CONTENT MATCHES "Sent Identification response to 127\\.0\\.0\\.1: teid=0x10004321")
	message(FATAL_ERROR "Missing Identification response log entry")
endif()