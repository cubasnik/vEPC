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
	message(FATAL_ERROR "TestS11Echo.cmake currently supports Windows only")
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
	COMMAND "${S11_GTPC_CLIENT_EXE}" echo 15
	WORKING_DIRECTORY "${SOURCE_DIR}"
	RESULT_VARIABLE ECHO_RESULT
	OUTPUT_VARIABLE ECHO_OUTPUT
	ERROR_VARIABLE ECHO_ERROR
	TIMEOUT 20
)

execute_process(
	COMMAND "${RUNTIME_CLI_EXE}" "iface_status S11" 20
	WORKING_DIRECTORY "${SOURCE_DIR}"
	RESULT_VARIABLE STATUS_RESULT
	OUTPUT_VARIABLE STATUS_OUTPUT
	ERROR_VARIABLE STATUS_ERROR
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

if(NOT ECHO_RESULT EQUAL 0)
	message(FATAL_ERROR "Failed to complete S11 Echo roundtrip: ${ECHO_OUTPUT} ${ECHO_ERROR}")
endif()
if(NOT STATUS_RESULT EQUAL 0)
	message(FATAL_ERROR "Failed to query S11 interface status via CLI: ${STATUS_ERROR}")
endif()
if(NOT STATE_RESULT EQUAL 0)
	message(FATAL_ERROR "Failed to query runtime state via CLI: ${STATE_ERROR}")
endif()

set(CLI_OUTPUT "${STATUS_OUTPUT}
${STATE_OUTPUT}")

if(NOT CLI_OUTPUT MATCHES "Interface S11 status:")
	message(FATAL_ERROR "Expected S11 interface status in CLI output. Output: ${CLI_OUTPUT}")
endif()
if(NOT CLI_OUTPUT MATCHES "Rx Packets:[ ]+[1-9][0-9]*")
	message(FATAL_ERROR "Expected non-zero packet counter in CLI output. Output: ${CLI_OUTPUT}")
endif()
if(NOT CLI_OUTPUT MATCHES "Last Peer:[ ]+127\\.0\\.0\\.1")
	message(FATAL_ERROR "Expected sender IP in CLI output. Output: ${CLI_OUTPUT}")
endif()
if(NOT CLI_OUTPUT MATCHES "PDP contexts: 0")
	message(FATAL_ERROR "Expected Echo path to leave PDP contexts empty. Output: ${CLI_OUTPUT}")
endif()
if(NOT CLI_OUTPUT MATCHES "PDP context details: none")
	message(FATAL_ERROR "Expected no PDP details after Echo path. Output: ${CLI_OUTPUT}")
endif()

if(NOT EXISTS "${LOG_FILE}")
	message(FATAL_ERROR "Expected log file ${LOG_FILE} to exist")
endif()

file(READ "${LOG_FILE}" LOG_CONTENT)
if(NOT LOG_CONTENT MATCHES "Parsed GTPv1-C header from 127\\.0\\.0\\.1: type=Echo Request")
	message(FATAL_ERROR "Missing S11 Echo parse log entry")
endif()
if(NOT LOG_CONTENT MATCHES "Sent Echo Response to 127\\.0\\.0\\.1: teid=0x00000000")
	message(FATAL_ERROR "Missing S11 Echo response log entry")
endif()