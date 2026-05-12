if(NOT DEFINED FCL_PACKAGE_TEST_SOURCE_DIR)
   message(FATAL_ERROR "FCL_PACKAGE_TEST_SOURCE_DIR is required")
endif()
if(NOT DEFINED FCL_PACKAGE_TEST_BINARY_DIR)
   message(FATAL_ERROR "FCL_PACKAGE_TEST_BINARY_DIR is required")
endif()
if(NOT DEFINED FCL_PACKAGE_TEST_PREFIX)
   message(FATAL_ERROR "FCL_PACKAGE_TEST_PREFIX is required")
endif()
if(NOT DEFINED FCL_PACKAGE_TEST_GENERATOR)
   message(FATAL_ERROR "FCL_PACKAGE_TEST_GENERATOR is required")
endif()

file(REMOVE_RECURSE "${FCL_PACKAGE_TEST_BINARY_DIR}")

set(
   _fcl_configure_options
   -DCMAKE_PREFIX_PATH=${FCL_PACKAGE_TEST_PREFIX}
   -DCMAKE_CXX_COMPILER=${FCL_PACKAGE_TEST_CXX_COMPILER}
   -DCMAKE_C_COMPILER=${FCL_PACKAGE_TEST_C_COMPILER}
   -DCMAKE_DISABLE_FIND_PACKAGE_Notcurses=ON
   -DCMAKE_DISABLE_FIND_PACKAGE_PkgConfig=ON
)

if(FCL_PACKAGE_TEST_OSX_SYSROOT)
   list(APPEND _fcl_configure_options -DCMAKE_OSX_SYSROOT=${FCL_PACKAGE_TEST_OSX_SYSROOT})
endif()

execute_process(
   COMMAND
      "${CMAKE_COMMAND}"
      -S "${FCL_PACKAGE_TEST_SOURCE_DIR}"
      -B "${FCL_PACKAGE_TEST_BINARY_DIR}"
      -G "${FCL_PACKAGE_TEST_GENERATOR}"
      ${_fcl_configure_options}
   RESULT_VARIABLE _fcl_configure_result
   OUTPUT_VARIABLE _fcl_configure_stdout
   ERROR_VARIABLE _fcl_configure_stderr
)

string(CONCAT _fcl_configure_log "${_fcl_configure_stdout}\n${_fcl_configure_stderr}")

if(_fcl_configure_result EQUAL 0)
   message(FATAL_ERROR "Expected find_package(FCL COMPONENTS all) to fail when notcurses discovery is disabled")
endif()

if(NOT _fcl_configure_log MATCHES "FCL tui component requires notcurses-core|FCL.*all|Could NOT find FCL")
   message(FATAL_ERROR "Unexpected configure failure:\n${_fcl_configure_log}")
endif()

message(STATUS "find_package(FCL COMPONENTS all) failed as expected when notcurses discovery is disabled")
