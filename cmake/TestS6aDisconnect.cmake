cmake_minimum_required(VERSION 3.10)

if(NOT DEFINED VEPC_EXE)
	message(FATAL_ERROR "VEPC_EXE is required")
endif()

if(NOT DEFINED RUNTIME_CLI_EXE)
	message(FATAL_ERROR "RUNTIME_CLI_EXE is required")
endif()

if(NOT DEFINED S6A_DIAMETER_CLIENT_EXE)
	message(FATAL_ERROR "S6A_DIAMETER_CLIENT_EXE is required")
endif()

if(NOT DEFINED SOURCE_DIR)
	message(FATAL_ERROR "SOURCE_DIR is required")
endif()

if(NOT WIN32)
	message(FATAL_ERROR "TestS6aDisconnect.cmake currently supports Windows only")
endif()

set(LOG_DIR "${SOURCE_DIR}/build/logs")
set(LOG_FILE "${LOG_DIR}/vepc.log")

file(MAKE_DIRECTORY "${LOG_DIR}")
file(REMOVE "${LOG_FILE}")

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
	COMMAND "${S6A_DIAMETER_CLIENT_EXE}" 15 disconnect
	WORKING_DIRECTORY "${SOURCE_DIR}"
	RESULT_VARIABLE SEND_RESULT
	OUTPUT_VARIABLE SEND_OUTPUT
	ERROR_VARIABLE SEND_ERROR
	TIMEOUT 20
)

execute_process(
	COMMAND "${RUNTIME_CLI_EXE}" "iface_status S6a" 20
	WORKING_DIRECTORY "${SOURCE_DIR}"
	RESULT_VARIABLE STATUS_RESULT
	OUTPUT_VARIABLE STATUS_OUTPUT
	ERROR_VARIABLE STATUS_ERROR
	TIMEOUT 20
)

execute_process(
	COMMAND taskkill /PID ${VEPC_PID} /F
	OUTPUT_QUIET
	ERROR_QUIET
)

if(NOT SEND_RESULT EQUAL 0)
	message(FATAL_ERROR "Failed to send S6a CER+DWR+DPR: ${SEND_OUTPUT} ${SEND_ERROR}")
endif()
if(NOT STATUS_RESULT EQUAL 0)
	message(FATAL_ERROR "Failed to query S6a interface status via CLI: ${STATUS_ERROR}")
endif()

set(CLI_OUTPUT "${STATUS_OUTPUT}")

if(NOT CLI_OUTPUT MATCHES "Last Message[ ]*:[ ]+Disconnect-Peer-Request \\(DPR\\)")
	message(FATAL_ERROR "Expected DPR as last message in CLI output. Output: ${CLI_OUTPUT}")
endif()
if(NOT CLI_OUTPUT MATCHES "Last Detail[ ]*:[ ]+origin_host=mme\\.vepc\\.local")
	message(FATAL_ERROR "Expected DPR origin_host detail in CLI output. Output: ${CLI_OUTPUT}")
endif()

if(NOT EXISTS "${LOG_FILE}")
	message(FATAL_ERROR "Expected log file ${LOG_FILE} to exist")
endif()

file(READ "${LOG_FILE}" LOG_CONTENT)
if(NOT LOG_CONTENT MATCHES "Parsed Diameter header from 127\\.0\\.0\\.1: command=Disconnect-Peer-Request \\(DPR\\)")
	message(FATAL_ERROR "Missing DPR parse log entry")
endif()
if(NOT LOG_CONTENT MATCHES "Sent Diameter response to 127\\.0\\.0\\.1: command=Disconnect-Peer-Answer \\(DPA\\)")
	message(FATAL_ERROR "Missing DPA response log entry")
endif()
