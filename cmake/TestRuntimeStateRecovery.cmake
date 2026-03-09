cmake_minimum_required(VERSION 3.10)

if(NOT DEFINED VEPC_EXE)
    message(FATAL_ERROR "VEPC_EXE is required")
endif()

if(NOT DEFINED SOURCE_DIR)
    message(FATAL_ERROR "SOURCE_DIR is required")
endif()

set(STATE_DIR "${SOURCE_DIR}/build/state")
set(LOG_DIR "${SOURCE_DIR}/build/logs")
set(STATE_FILE "${STATE_DIR}/runtime_state.json")
set(LOG_FILE "${LOG_DIR}/vepc.log")

file(MAKE_DIRECTORY "${STATE_DIR}")
file(MAKE_DIRECTORY "${LOG_DIR}")

file(GLOB OLD_CORRUPT_FILES "${STATE_DIR}/runtime_state.corrupt-*.json")
foreach(old_file IN LISTS OLD_CORRUPT_FILES)
    file(REMOVE "${old_file}")
endforeach()

set(STALE_SUFFIXES 000001 000002 000003 000004 000005 000006 000007)
foreach(suffix IN LISTS STALE_SUFFIXES)
    file(WRITE "${STATE_DIR}/runtime_state.corrupt-20200101-${suffix}.json" "stale-${suffix}")
endforeach()

file(REMOVE "${LOG_FILE}")
file(WRITE "${STATE_FILE}" "{\"schema_version\":2,\"saved_at\":\"broken\",\"ue_contexts\":[{\"imsi\":\"123\"}],\"pdp_contexts\":[]}")

execute_process(
    COMMAND "${VEPC_EXE}"
    WORKING_DIRECTORY "${SOURCE_DIR}"
    RESULT_VARIABLE VEPc_RESULT
    OUTPUT_VARIABLE VEPC_OUTPUT
    ERROR_VARIABLE VEPC_ERROR
    TIMEOUT 3
)

if(WIN32)
    execute_process(
        COMMAND taskkill /IM vepc.exe /F
        OUTPUT_QUIET
        ERROR_QUIET
    )
endif()

file(GLOB CORRUPT_FILES "${STATE_DIR}/runtime_state.corrupt-*.json")
list(LENGTH CORRUPT_FILES CORRUPT_COUNT)
if(CORRUPT_COUNT LESS 1)
    message(FATAL_ERROR "Expected quarantined runtime state file, found none. Output: ${VEPC_OUTPUT} ${VEPC_ERROR}")
endif()
if(CORRUPT_COUNT GREATER 5)
    message(FATAL_ERROR "Expected old corrupt runtime state files to be pruned to 5, found ${CORRUPT_COUNT}")
endif()

set(HAS_FRESH_QUARANTINE FALSE)
foreach(corrupt_file IN LISTS CORRUPT_FILES)
    if(NOT corrupt_file MATCHES "runtime_state\\.corrupt-20200101-[0-9]{6}\\.json$")
        set(HAS_FRESH_QUARANTINE TRUE)
    endif()
endforeach()
if(NOT HAS_FRESH_QUARANTINE)
    message(FATAL_ERROR "Expected freshly quarantined runtime state file in addition to stale fixtures")
endif()

if(EXISTS "${STATE_FILE}")
    file(READ "${STATE_FILE}" CURRENT_STATE)
    if(CURRENT_STATE MATCHES "broken")
        message(FATAL_ERROR "Corrupt runtime_state.json was not replaced or removed")
    endif()
endif()

if(NOT EXISTS "${LOG_FILE}")
    message(FATAL_ERROR "Expected log file ${LOG_FILE} to exist")
endif()

file(READ "${LOG_FILE}" LOG_CONTENT)
if(NOT LOG_CONTENT MATCHES "Corrupt runtime state moved to")
    message(FATAL_ERROR "Missing quarantine log entry in vepc.log")
endif()
if(NOT LOG_CONTENT MATCHES "Failed to load runtime state")
    message(FATAL_ERROR "Missing load failure log entry in vepc.log")
endif()
