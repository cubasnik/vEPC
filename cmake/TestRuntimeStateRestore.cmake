cmake_minimum_required(VERSION 3.10)

if(NOT DEFINED VEPC_EXE)
	message(FATAL_ERROR "VEPC_EXE is required")
endif()

if(NOT DEFINED RUNTIME_CLI_EXE)
	message(FATAL_ERROR "RUNTIME_CLI_EXE is required")
endif()

if(NOT DEFINED SOURCE_DIR)
	message(FATAL_ERROR "SOURCE_DIR is required")
endif()

if(NOT WIN32)
	message(FATAL_ERROR "TestRuntimeStateRestore.cmake currently supports Windows only")
endif()

set(STATE_DIR "${SOURCE_DIR}/build/state")
set(LOG_DIR "${SOURCE_DIR}/build/logs")
set(STATE_FILE "${STATE_DIR}/runtime_state.json")
set(LOG_FILE "${LOG_DIR}/vepc.log")

file(MAKE_DIRECTORY "${STATE_DIR}")
file(MAKE_DIRECTORY "${LOG_DIR}")
file(REMOVE "${LOG_FILE}")

set(STATE_CONTENT [=[{
  "schema_version": 2,
  "saved_at": "2026-03-09 12:00:00",
  "metadata": {
	"cli_endpoint": "127.0.0.1:5555",
	"status": "Running",
	"interface_admin_state_count": 0
  },
  "interface_admin_state": [],
  "pdp_contexts": [
	{
	  "teid": 268452641,
	  "sequence": 17185,
	  "pdp_type": 33,
	  "has_pdp_type": true,
	  "last_message_type": 16,
	  "peer_ip": "127.0.0.1",
	  "ggsn_ip": "10.23.42.5",
	  "imsi": "123456789012345",
	  "apn": "internet",
	  "updated_at": "2026-03-09 12:00:01"
	}
  ],
  "ue_contexts": [
	{
	  "imsi": "123456789012345",
	  "guti": "guti-restore-01",
	  "peer_id": "restore-peer",
	  "authenticated": true,
	  "auth_request_sent": true,
	  "auth_response_received": true,
	  "security_mode_command_sent": true,
	  "security_mode_complete": true,
	  "attach_accept_sent": true,
	  "attach_complete_received": true,
	  "attached": true,
	  "service_request_received": true,
	  "service_accept_sent": true,
	  "service_active": true,
	  "service_resume_request_received": false,
	  "service_resume_accept_sent": false,
	  "service_release_request_received": false,
	  "service_release_complete_sent": false,
	  "detach_request_received": false,
	  "detach_accept_sent": false,
	  "detached": false,
	  "tracking_area_update_request_received": false,
	  "tracking_area_update_accept_sent": false,
	  "tracking_area_update_complete_received": false,
	  "last_nas_message_type": 77,
	  "has_last_nas_message_type": true,
	  "last_s1ap_procedure": 12,
	  "security_context_id": 7,
	  "has_security_context_id": true,
	  "selected_nas_algorithm": 1,
	  "has_selected_nas_algorithm": true,
	  "default_bearer_id": 5,
	  "has_default_bearer_id": true,
	  "tracking_area_code": 0,
	  "has_tracking_area_code": false,
	  "auth_flow": "service-active",
	  "updated_at": "2026-03-09 12:00:02"
	}
  ]
}
]=])
file(WRITE "${STATE_FILE}" "${STATE_CONTENT}")

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
	COMMAND "${RUNTIME_CLI_EXE}" state 20
	WORKING_DIRECTORY "${SOURCE_DIR}"
	RESULT_VARIABLE CLI_RESULT
	OUTPUT_VARIABLE CLI_OUTPUT
	ERROR_VARIABLE CLI_ERROR
	TIMEOUT 20
)

execute_process(
	COMMAND taskkill /PID ${VEPC_PID} /F
	OUTPUT_QUIET
	ERROR_QUIET
)

if(NOT CLI_RESULT EQUAL 0)
	message(FATAL_ERROR "Failed to query restored state via CLI: ${CLI_ERROR}")
endif()

if(NOT CLI_OUTPUT MATCHES "PDP contexts: 1")
	message(FATAL_ERROR "Expected restored PDP context in state output. Output: ${CLI_OUTPUT}")
endif()
if(NOT CLI_OUTPUT MATCHES "UE contexts:[ ]+1")
	message(FATAL_ERROR "Expected restored UE context in state output. Output: ${CLI_OUTPUT}")
endif()
if(NOT CLI_OUTPUT MATCHES "IMSI: 123456789012345")
	message(FATAL_ERROR "Expected restored IMSI in state output. Output: ${CLI_OUTPUT}")
endif()
if(NOT CLI_OUTPUT MATCHES "APN: internet")
	message(FATAL_ERROR "Expected restored APN in state output. Output: ${CLI_OUTPUT}")
endif()
if(NOT CLI_OUTPUT MATCHES "Auth Flow: service-active")
	message(FATAL_ERROR "Expected restored UE auth flow in state output. Output: ${CLI_OUTPUT}")
endif()
if(NOT CLI_OUTPUT MATCHES "Default Bearer ID: 0x05")
	message(FATAL_ERROR "Expected restored bearer in state output. Output: ${CLI_OUTPUT}")
endif()

if(NOT EXISTS "${LOG_FILE}")
	message(FATAL_ERROR "Expected log file ${LOG_FILE} to exist")
endif()

file(READ "${LOG_FILE}" LOG_CONTENT)
if(NOT LOG_CONTENT MATCHES "Runtime state loaded from .+\(ue_contexts=1, pdp_contexts=1\)")
	message(FATAL_ERROR "Missing runtime restore log entry in vepc.log")
endif()
