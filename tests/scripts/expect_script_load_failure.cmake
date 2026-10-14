# Runs vis_app with a script path that does not exist. A load failure must print a message, skip the
# main loop and exit with code 2 (it used to be possible to hang or exit 0). Usage:
#   cmake -DAPP=<vis_app> -P expect_script_load_failure.cmake
execute_process(
  COMMAND "${APP}" --size 900x600 --script "${CMAKE_CURRENT_LIST_DIR}/does_not_exist.script"
  RESULT_VARIABLE code
  ERROR_VARIABLE err
  OUTPUT_VARIABLE out
  TIMEOUT 60)
if(NOT code EQUAL 2)
  message(FATAL_ERROR "expected exit code 2, got '${code}'\n${out}\n${err}")
endif()
if(NOT err MATCHES "cannot read script")
  message(FATAL_ERROR "expected 'cannot read script' on stderr, got:\n${err}")
endif()
