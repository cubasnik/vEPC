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
	message(FATAL_ERROR "TestS11Telemetry.cmake currently supports Windows only")
endif()

set(LOG_DIR "${SOURCE_DIR}/build/logs")
set(LOG_FILE "${LOG_DIR}/vepc.log")
set(STATE_DIR "${SOURCE_DIR}/build/state")
set(STATE_FILE "${STATE_DIR}/runtime_state.json")

file(MAKE_DIRECTORY "${LOG_DIR}")
file(MAKE_DIRECTORY "${STATE_DIR}")
file(REMOVE "${LOG_FILE}")
file(REMOVE "${STATE_FILE}")

set(START_SCRIPT "$proc = Start-Process -FilePath '${VEPC_EXE}' -PassThru\n$proc.Id\n")

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
	COMMAND "${S11_GTPC_CLIENT_EXE}" 15
	WORKING_DIRECTORY "${SOURCE_DIR}"
	RESULT_VARIABLE SEND_RESULT
	OUTPUT_VARIABLE SEND_OUTPUT
	ERROR_VARIABLE SEND_ERROR
	TIMEOUT 20
)
if(NOT SEND_RESULT EQUAL 0)
	execute_process(COMMAND taskkill /PID ${VEPC_PID} /F OUTPUT_QUIET ERROR_QUIET)
	message(FATAL_ERROR "Failed to complete S11 GTP-C roundtrip: ${SEND_OUTPUT} ${SEND_ERROR}")
endif()

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

if(NOT STATUS_RESULT EQUAL 0)
	message(FATAL_ERROR "Failed to query S11 interface status via CLI: ${STATUS_ERROR}")
endif()
if(NOT STATE_RESULT EQUAL 0)
	message(FATAL_ERROR "Failed to query runtime state via CLI: ${STATE_ERROR}")
endif()

set(CLI_OUTPUT "${STATUS_OUTPUT}\n${STATE_OUTPUT}")

if(NOT CLI_OUTPUT MATCHES "Interface S11 status:")
	message(FATAL_ERROR "Expected S11 interface status in CLI output. Output: ${CLI_OUTPUT}")
endif()
if(NOT CLI_OUTPUT MATCHES "Effective Bind: 127\\.0\\.0\\.1:2123")
	message(FATAL_ERROR "Expected effective S11 bind IP in CLI output. Output: ${CLI_OUTPUT}")
endif()
if(NOT CLI_OUTPUT MATCHES "Rx Packets:[ ]+[1-9][0-9]*")
	message(FATAL_ERROR "Expected non-zero packet counter in CLI output. Output: ${CLI_OUTPUT}")
endif()
if(NOT CLI_OUTPUT MATCHES "Rx Bytes:[ ]+[1-9][0-9]*")
	message(FATAL_ERROR "Expected non-zero byte counter in CLI output. Output: ${CLI_OUTPUT}")
endif()
if(NOT CLI_OUTPUT MATCHES "Last Peer:[ ]+127\\.0\\.0\\.1")
	message(FATAL_ERROR "Expected sender IP in CLI output. Output: ${CLI_OUTPUT}")
endif()
if(NOT CLI_OUTPUT MATCHES "Endpoint telemetry:")
	message(FATAL_ERROR "Expected endpoint telemetry block in state output. Output: ${CLI_OUTPUT}")
endif()
if(NOT CLI_OUTPUT MATCHES "- Name: S11")
	message(FATAL_ERROR "Expected S11 endpoint telemetry entry in state output. Output: ${CLI_OUTPUT}")
endif()
if(NOT CLI_OUTPUT MATCHES "PDP contexts: 1")
	message(FATAL_ERROR "Expected PDP context created by S11 Create PDP request. Output: ${CLI_OUTPUT}")
endif()
if(NOT CLI_OUTPUT MATCHES "- TEID: 0x10004321")
	message(FATAL_ERROR "Expected assigned TEID from S11 Create PDP flow. Output: ${CLI_OUTPUT}")
endif()
if(NOT CLI_OUTPUT MATCHES "IMSI: 123456789012345")
	message(FATAL_ERROR "Expected IMSI from parsed S11 Create PDP request. Output: ${CLI_OUTPUT}")
endif()
if(NOT CLI_OUTPUT MATCHES "APN: internet")
	message(FATAL_ERROR "Expected APN from parsed S11 Create PDP request. Output: ${CLI_OUTPUT}")
endif()

if(NOT EXISTS "${LOG_FILE}")
	message(FATAL_ERROR "Expected log file ${LOG_FILE} to exist")
endif()

file(READ "${LOG_FILE}" LOG_CONTENT)
if(NOT LOG_CONTENT MATCHES "Interface endpoint S11 ready on 127\\.0\\.0\\.1:2123")
	message(FATAL_ERROR "Missing S11 endpoint startup log entry")
endif()
if(NOT LOG_CONTENT MATCHES "Parsed GTPv1-C header from 127\\.0\\.0\\.1: type=Create PDP Context Request")
	message(FATAL_ERROR "Missing S11 GTP-C parse log entry")
endif()
if(NOT LOG_CONTENT MATCHES "Create PDP request parsed from 127\\.0\\.0\\.1: imsi=123456789012345, apn=internet")
	message(FATAL_ERROR "Missing Create PDP request details in log")
endif()
if(NOT LOG_CONTENT MATCHES "Sent Create PDP response to 127\\.0\\.0\\.1: teid=0x10004321")
	message(FATAL_ERROR "Missing Create PDP response log entry")
endif()
