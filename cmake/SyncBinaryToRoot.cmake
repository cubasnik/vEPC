if(NOT DEFINED SOURCE OR NOT DEFINED DESTINATION)
    message(FATAL_ERROR "SyncBinaryToRoot.cmake requires SOURCE and DESTINATION")
endif()

if(NOT DEFINED LABEL)
    get_filename_component(LABEL "${DESTINATION}" NAME)
endif()

if(NOT EXISTS "${SOURCE}")
    message(VERBOSE "Skipping root sync for ${LABEL}: source file not found: ${SOURCE}")
    return()
endif()

execute_process(
    COMMAND "${CMAKE_COMMAND}" -E copy_if_different "${SOURCE}" "${DESTINATION}"
    RESULT_VARIABLE sync_result
    ERROR_VARIABLE sync_error
)

if(NOT sync_result EQUAL 0)
    string(REPLACE "\r" " " sync_error "${sync_error}")
    string(REPLACE "\n" " " sync_error "${sync_error}")
    message(VERBOSE "Skipping root sync for ${LABEL}; destination may be locked: ${sync_error}")
endif()