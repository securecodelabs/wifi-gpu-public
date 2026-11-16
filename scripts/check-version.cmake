execute_process(COMMAND "${ENGINE}" --version
    RESULT_VARIABLE result
    OUTPUT_VARIABLE actual
    ERROR_VARIABLE error
    OUTPUT_STRIP_TRAILING_WHITESPACE
    TIMEOUT 15)
if(NOT result EQUAL 0 OR NOT actual STREQUAL EXPECTED_VERSION)
    message(FATAL_ERROR "Engine version mismatch: expected '${EXPECTED_VERSION}', got '${actual}' (exit ${result}): ${error}")
endif()
