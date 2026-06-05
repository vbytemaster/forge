find_package(Python3 REQUIRED COMPONENTS Interpreter)

add_executable(
   test_fcl
   unit/main.cpp
   core/test_chrono.cpp
   core/test_escape_str.cpp
   variant/test_static_variant.cpp
   variant/test_variant.cpp
   variant/test_variant_dynamic_bitset.cpp
   variant/test_variant_estimated_size.cpp
   raw/test_raw.cpp
   json/test_json.cpp
   crypto/test_base32.cpp
   crypto/test_base64.cpp
   crypto/test_base58.cpp
   crypto/test_blake2.cpp
   crypto/test_bls.cpp
   crypto/test_bls_legacy.cpp
   crypto/test_cypher_suites.cpp
   crypto/test_hash_functions.cpp
   crypto/test_secp256k1_recover.cpp
   crypto/test_modular_arithmetic.cpp
   crypto/test_symmetric.cpp
   crypto/test_webauthn.cpp
)

target_link_libraries(test_fcl fcl Boost::included_unit_test_framework)
add_test(NAME test_fcl COMMAND test_fcl WORKING_DIRECTORY ${CMAKE_BINARY_DIR})

add_executable(
   test_fcl_exceptions
   unit/main.cpp
   exceptions/exception_tests.cpp
)
target_link_libraries(test_fcl_exceptions fcl Boost::included_unit_test_framework)
add_test(NAME test_fcl_exceptions COMMAND test_fcl_exceptions WORKING_DIRECTORY ${CMAKE_BINARY_DIR})

add_executable(
   test_fcl_log
   unit/main.cpp
   log/log_tests.cpp
)
target_link_libraries(test_fcl_log fcl Boost::included_unit_test_framework)
add_test(NAME test_fcl_log COMMAND test_fcl_log WORKING_DIRECTORY ${CMAKE_BINARY_DIR})

add_executable(
   test_fcl_json
   unit/main.cpp
   json/test_json.cpp
)
target_link_libraries(test_fcl_json fcl Boost::included_unit_test_framework)
add_test(NAME test_fcl_json COMMAND test_fcl_json WORKING_DIRECTORY ${CMAKE_BINARY_DIR})

add_executable(
   test_fcl_raw
   unit/main.cpp
   raw/test_raw.cpp
)
target_link_libraries(test_fcl_raw fcl Boost::included_unit_test_framework)
add_test(NAME test_fcl_raw COMMAND test_fcl_raw WORKING_DIRECTORY ${CMAKE_BINARY_DIR})

add_executable(
   test_fcl_crypto
   unit/main.cpp
   crypto/test_base32.cpp
   crypto/test_base64.cpp
   crypto/test_base58.cpp
   crypto/test_blake2.cpp
   crypto/test_bls.cpp
   crypto/test_bls_legacy.cpp
   crypto/test_cypher_suites.cpp
   crypto/test_hash_functions.cpp
   crypto/test_secp256k1_recover.cpp
   crypto/test_modular_arithmetic.cpp
   crypto/test_symmetric.cpp
   crypto/test_webauthn.cpp
)
target_link_libraries(test_fcl_crypto fcl Boost::included_unit_test_framework)
add_test(NAME test_fcl_crypto COMMAND test_fcl_crypto WORKING_DIRECTORY ${CMAKE_BINARY_DIR})

add_executable(
   test_fcl_multiformats
   unit/main.cpp
   multiformats/multiformats_tests.cpp
)
target_link_libraries(test_fcl_multiformats fcl Boost::included_unit_test_framework)
add_test(NAME test_fcl_multiformats COMMAND test_fcl_multiformats WORKING_DIRECTORY ${CMAKE_BINARY_DIR})

add_executable(
   test_fcl_asio
   unit/main.cpp
   asio/task_scheduler_tests.cpp
)
target_link_libraries(test_fcl_asio fcl Boost::included_unit_test_framework)
add_test(NAME test_fcl_asio COMMAND test_fcl_asio WORKING_DIRECTORY ${CMAKE_BINARY_DIR})

add_executable(
   test_fcl_transport
   unit/main.cpp
   transport/transport_tests.cpp
)
target_link_libraries(test_fcl_transport fcl Boost::included_unit_test_framework)
add_test(NAME test_fcl_transport COMMAND test_fcl_transport WORKING_DIRECTORY ${CMAKE_BINARY_DIR})

add_executable(
   test_fcl_tcp
   unit/main.cpp
   tcp/tcp_tests.cpp
)
target_link_libraries(test_fcl_tcp fcl Boost::included_unit_test_framework)
add_test(NAME test_fcl_tcp COMMAND test_fcl_tcp WORKING_DIRECTORY ${CMAKE_BINARY_DIR})

add_executable(
   test_fcl_stcp
   unit/main.cpp
   stcp/stcp_tests.cpp
)
target_link_libraries(test_fcl_stcp fcl Boost::included_unit_test_framework OpenSSL::SSL)
add_test(NAME test_fcl_stcp COMMAND test_fcl_stcp WORKING_DIRECTORY ${CMAKE_BINARY_DIR})

add_executable(
   test_fcl_yamux
   unit/main.cpp
   yamux/yamux_tests.cpp
)
target_link_libraries(test_fcl_yamux fcl Boost::included_unit_test_framework)
add_test(NAME test_fcl_yamux COMMAND test_fcl_yamux WORKING_DIRECTORY ${CMAKE_BINARY_DIR})

add_executable(
   test_fcl_quic
   unit/main.cpp
   quic/quic_transport_tests.cpp
)
target_link_libraries(test_fcl_quic fcl Boost::included_unit_test_framework)
add_test(NAME test_fcl_quic COMMAND test_fcl_quic WORKING_DIRECTORY ${CMAKE_BINARY_DIR})

add_executable(
   test_fcl_app
   unit/main.cpp
   app/app_tests.cpp
)
target_link_libraries(test_fcl_app fcl Boost::included_unit_test_framework)
add_test(NAME test_fcl_app COMMAND test_fcl_app WORKING_DIRECTORY ${CMAKE_BINARY_DIR})

add_executable(
   test_fcl_schema
   unit/main.cpp
   schema/schema_tests.cpp
)
target_link_libraries(test_fcl_schema fcl Boost::included_unit_test_framework)
add_test(NAME test_fcl_schema COMMAND test_fcl_schema WORKING_DIRECTORY ${CMAKE_BINARY_DIR})

add_executable(
   test_fcl_config
   unit/main.cpp
   config/config_tests.cpp
)
target_link_libraries(test_fcl_config fcl Boost::included_unit_test_framework)
add_test(NAME test_fcl_config COMMAND test_fcl_config WORKING_DIRECTORY ${CMAKE_BINARY_DIR})

add_executable(
   test_fcl_yaml
   unit/main.cpp
   yaml/yaml_tests.cpp
)
target_link_libraries(test_fcl_yaml fcl Boost::included_unit_test_framework)
add_test(NAME test_fcl_yaml COMMAND test_fcl_yaml WORKING_DIRECTORY ${CMAKE_BINARY_DIR})

add_executable(
   test_fcl_program_options
   unit/main.cpp
   program_options/program_options_tests.cpp
)
target_link_libraries(test_fcl_program_options fcl Boost::included_unit_test_framework)
add_test(NAME test_fcl_program_options COMMAND test_fcl_program_options WORKING_DIRECTORY ${CMAKE_BINARY_DIR})

add_executable(
   test_fcl_env
   unit/main.cpp
   env/env_tests.cpp
)
target_link_libraries(test_fcl_env fcl Boost::included_unit_test_framework)
add_test(NAME test_fcl_env COMMAND test_fcl_env WORKING_DIRECTORY ${CMAKE_BINARY_DIR})

add_executable(
   test_fcl_api
   unit/main.cpp
   api/api_tests.cpp
   api/api_module_smoke_tests.cpp
)
target_link_libraries(test_fcl_api fcl Boost::included_unit_test_framework)
add_test(NAME test_fcl_api COMMAND test_fcl_api WORKING_DIRECTORY ${CMAKE_BINARY_DIR})

add_executable(
   test_fcl_api_transport
   unit/main.cpp
   api/api_transport_tests.cpp
)
target_link_libraries(test_fcl_api_transport fcl Boost::included_unit_test_framework)
add_test(NAME test_fcl_api_transport COMMAND test_fcl_api_transport WORKING_DIRECTORY ${CMAKE_BINARY_DIR})

add_executable(
   test_fcl_http_websocket
   unit/main.cpp
   http_websocket/network_tests.cpp
)
target_link_libraries(test_fcl_http_websocket fcl Boost::included_unit_test_framework)
add_test(NAME test_fcl_http_websocket COMMAND test_fcl_http_websocket WORKING_DIRECTORY ${CMAKE_BINARY_DIR})

add_executable(
   test_fcl_quic_p2p
   unit/main.cpp
   quic_p2p/quic_tests.cpp
   quic_p2p/p2p_tests.cpp
)
target_link_libraries(test_fcl_quic_p2p fcl Boost::included_unit_test_framework)
target_compile_definitions(test_fcl_quic_p2p PRIVATE FCL_HAS_ROCKSDB=$<BOOL:${FCL_HAS_ROCKSDB}>)
add_test(NAME test_fcl_quic_p2p COMMAND test_fcl_quic_p2p WORKING_DIRECTORY ${CMAKE_BINARY_DIR})

add_executable(
   fcl_interop_fixture
   libp2p_interop/fcl_interop_fixture.cpp
)
target_link_libraries(fcl_interop_fixture fcl)

set(_fcl_libp2p_interop_enabled 0)
if(FCL_ENABLE_LIBP2P_INTEROP)
   set(_fcl_libp2p_interop_enabled 1)
endif()

add_custom_target(
   test_fcl_libp2p_interop
   COMMAND ${Python3_EXECUTABLE}
      ${CMAKE_CURRENT_SOURCE_DIR}/libp2p_interop/runner.py
      --enabled ${_fcl_libp2p_interop_enabled}
      --fcl-fixture $<TARGET_FILE:fcl_interop_fixture>
      --source-dir ${CMAKE_CURRENT_SOURCE_DIR}/libp2p_interop
      --build-dir ${CMAKE_CURRENT_BINARY_DIR}/libp2p_interop
      --donors-root ${FCL_LIBP2P_DONORS_ROOT}
   DEPENDS fcl_interop_fixture
)
add_test(
   NAME test_fcl_libp2p_interop
   COMMAND ${Python3_EXECUTABLE}
      ${CMAKE_CURRENT_SOURCE_DIR}/libp2p_interop/runner.py
      --enabled ${_fcl_libp2p_interop_enabled}
      --fcl-fixture $<TARGET_FILE:fcl_interop_fixture>
      --source-dir ${CMAKE_CURRENT_SOURCE_DIR}/libp2p_interop
      --build-dir ${CMAKE_CURRENT_BINARY_DIR}/libp2p_interop
      --donors-root ${FCL_LIBP2P_DONORS_ROOT}
)
set_tests_properties(test_fcl_libp2p_interop PROPERTIES SKIP_REGULAR_EXPRESSION "^SKIP:")

add_test(
   NAME test_fcl_libp2p_donor_matrix
   COMMAND ${Python3_EXECUTABLE}
      ${CMAKE_CURRENT_SOURCE_DIR}/libp2p_interop/check_donor_matrix.py
      ${CMAKE_CURRENT_SOURCE_DIR}/libp2p_interop/donor_cases.json
)

add_executable(
   test_fcl_plugins
   unit/main.cpp
   plugins/plugins_tests.cpp
)
target_link_libraries(test_fcl_plugins fcl Boost::included_unit_test_framework)
add_test(NAME test_fcl_plugins COMMAND test_fcl_plugins WORKING_DIRECTORY ${CMAKE_BINARY_DIR})

if(TARGET fcl_tui)
   add_executable(
      test_fcl_tui
      unit/main.cpp
      tui/tui_tests.cpp
   )
   target_link_libraries(test_fcl_tui fcl fcl_tui Boost::included_unit_test_framework)
   add_test(NAME test_fcl_tui COMMAND test_fcl_tui WORKING_DIRECTORY ${CMAKE_BINARY_DIR})
else()
   message(STATUS "Skipping test_fcl_tui: fcl_tui is not available")
endif()

set(FCL_PACKAGE_INSTALL_PREFIX "${CMAKE_BINARY_DIR}/fcl-package-install")
set(
   FCL_PACKAGE_CONSUMER_BUILD_OPTIONS
   -DCMAKE_PREFIX_PATH=${FCL_PACKAGE_INSTALL_PREFIX}
   -DCMAKE_CXX_COMPILER=${CMAKE_CXX_COMPILER}
   -DCMAKE_C_COMPILER=${CMAKE_C_COMPILER}
   -DCMAKE_OSX_SYSROOT=${CMAKE_OSX_SYSROOT}
)

add_custom_target(
   test_fcl_package_install
   COMMAND ${CMAKE_COMMAND} --install ${CMAKE_BINARY_DIR} --prefix ${FCL_PACKAGE_INSTALL_PREFIX} --component dev
)
add_custom_target(
   test_fcl_package_consumer
   COMMAND ${CMAKE_CTEST_COMMAND}
      --build-and-test
      ${CMAKE_CURRENT_SOURCE_DIR}/package_consumer
      ${CMAKE_BINARY_DIR}/fcl-package-consumer
      --build-generator ${CMAKE_GENERATOR}
      --build-options ${FCL_PACKAGE_CONSUMER_BUILD_OPTIONS}
      --test-command fcl_package_consumer
   DEPENDS test_fcl_package_install
)
add_custom_target(
   test_fcl_package_core_lightweight
   COMMAND ${CMAKE_CTEST_COMMAND}
      --build-and-test
      ${CMAKE_CURRENT_SOURCE_DIR}/package_core_lightweight
      ${CMAKE_BINARY_DIR}/fcl-package-core-lightweight
      --build-generator ${CMAKE_GENERATOR}
      --build-options
         ${FCL_PACKAGE_CONSUMER_BUILD_OPTIONS}
         -DCMAKE_DISABLE_FIND_PACKAGE_OpenSSL=ON
         -DCMAKE_DISABLE_FIND_PACKAGE_Ngtcp2=ON
         -DCMAKE_DISABLE_FIND_PACKAGE_glaze=ON
         -DCMAKE_DISABLE_FIND_PACKAGE_Notcurses=ON
      --test-command fcl_package_core_lightweight
   DEPENDS test_fcl_package_install
)
add_custom_target(
   test_fcl_package_env_lightweight
   COMMAND ${CMAKE_CTEST_COMMAND}
      --build-and-test
      ${CMAKE_CURRENT_SOURCE_DIR}/package_env_lightweight
      ${CMAKE_BINARY_DIR}/fcl-package-env-lightweight
      --build-generator ${CMAKE_GENERATOR}
      --build-options
         ${FCL_PACKAGE_CONSUMER_BUILD_OPTIONS}
         -DCMAKE_DISABLE_FIND_PACKAGE_OpenSSL=ON
         -DCMAKE_DISABLE_FIND_PACKAGE_Ngtcp2=ON
         -DCMAKE_DISABLE_FIND_PACKAGE_glaze=ON
         -DCMAKE_DISABLE_FIND_PACKAGE_Notcurses=ON
      --test-command fcl_package_env_lightweight
   DEPENDS test_fcl_package_install
)
add_custom_target(
   test_fcl_package_api_lightweight
   COMMAND ${CMAKE_CTEST_COMMAND}
      --build-and-test
      ${CMAKE_CURRENT_SOURCE_DIR}/package_api_lightweight
      ${CMAKE_BINARY_DIR}/fcl-package-api-lightweight
      --build-generator ${CMAKE_GENERATOR}
      --build-options
         ${FCL_PACKAGE_CONSUMER_BUILD_OPTIONS}
         -DCMAKE_DISABLE_FIND_PACKAGE_OpenSSL=ON
         -DCMAKE_DISABLE_FIND_PACKAGE_Ngtcp2=ON
         -DCMAKE_DISABLE_FIND_PACKAGE_glaze=ON
         -DCMAKE_DISABLE_FIND_PACKAGE_Notcurses=ON
      --test-command fcl_package_api_lightweight
   DEPENDS test_fcl_package_install
)
add_custom_target(
   test_fcl_package_quic_component
   COMMAND ${CMAKE_CTEST_COMMAND}
      --build-and-test
      ${CMAKE_CURRENT_SOURCE_DIR}/package_quic_component
      ${CMAKE_BINARY_DIR}/fcl-package-quic-component
      --build-generator ${CMAKE_GENERATOR}
      --build-options ${FCL_PACKAGE_CONSUMER_BUILD_OPTIONS}
      --test-command fcl_package_quic_component
   DEPENDS test_fcl_package_install
)
add_custom_target(
   test_fcl_package_all_missing_dependency
   COMMAND ${CMAKE_COMMAND}
      -DFCL_PACKAGE_TEST_SOURCE_DIR=${CMAKE_CURRENT_SOURCE_DIR}/package_all_missing_dependency
      -DFCL_PACKAGE_TEST_BINARY_DIR=${CMAKE_BINARY_DIR}/fcl-package-all-missing-dependency
      -DFCL_PACKAGE_TEST_PREFIX=${FCL_PACKAGE_INSTALL_PREFIX}
      -DFCL_PACKAGE_TEST_GENERATOR=${CMAKE_GENERATOR}
      -DFCL_PACKAGE_TEST_CXX_COMPILER=${CMAKE_CXX_COMPILER}
      -DFCL_PACKAGE_TEST_C_COMPILER=${CMAKE_C_COMPILER}
      -DFCL_PACKAGE_TEST_OSX_SYSROOT=${CMAKE_OSX_SYSROOT}
      -P ${CMAKE_CURRENT_SOURCE_DIR}/package_all_missing_dependency/expect_configure_failure.cmake
   DEPENDS test_fcl_package_install
)

add_test(
   NAME test_fcl_package_install
   COMMAND ${CMAKE_COMMAND} --install ${CMAKE_BINARY_DIR} --prefix ${FCL_PACKAGE_INSTALL_PREFIX} --component dev
)
add_test(
   NAME test_fcl_package_consumer
   COMMAND ${CMAKE_CTEST_COMMAND}
      --build-and-test
      ${CMAKE_CURRENT_SOURCE_DIR}/package_consumer
      ${CMAKE_BINARY_DIR}/fcl-package-consumer
      --build-generator ${CMAKE_GENERATOR}
      --build-options ${FCL_PACKAGE_CONSUMER_BUILD_OPTIONS}
      --test-command fcl_package_consumer
)
set_tests_properties(test_fcl_package_consumer PROPERTIES DEPENDS test_fcl_package_install)
add_test(
   NAME test_fcl_package_core_lightweight
   COMMAND ${CMAKE_CTEST_COMMAND}
      --build-and-test
      ${CMAKE_CURRENT_SOURCE_DIR}/package_core_lightweight
      ${CMAKE_BINARY_DIR}/fcl-package-core-lightweight
      --build-generator ${CMAKE_GENERATOR}
      --build-options
         ${FCL_PACKAGE_CONSUMER_BUILD_OPTIONS}
         -DCMAKE_DISABLE_FIND_PACKAGE_OpenSSL=ON
         -DCMAKE_DISABLE_FIND_PACKAGE_Ngtcp2=ON
         -DCMAKE_DISABLE_FIND_PACKAGE_glaze=ON
         -DCMAKE_DISABLE_FIND_PACKAGE_Notcurses=ON
      --test-command fcl_package_core_lightweight
)
set_tests_properties(test_fcl_package_core_lightweight PROPERTIES DEPENDS test_fcl_package_install)
add_test(
   NAME test_fcl_package_env_lightweight
   COMMAND ${CMAKE_CTEST_COMMAND}
      --build-and-test
      ${CMAKE_CURRENT_SOURCE_DIR}/package_env_lightweight
      ${CMAKE_BINARY_DIR}/fcl-package-env-lightweight
      --build-generator ${CMAKE_GENERATOR}
      --build-options
         ${FCL_PACKAGE_CONSUMER_BUILD_OPTIONS}
         -DCMAKE_DISABLE_FIND_PACKAGE_OpenSSL=ON
         -DCMAKE_DISABLE_FIND_PACKAGE_Ngtcp2=ON
         -DCMAKE_DISABLE_FIND_PACKAGE_glaze=ON
         -DCMAKE_DISABLE_FIND_PACKAGE_Notcurses=ON
      --test-command fcl_package_env_lightweight
)
set_tests_properties(test_fcl_package_env_lightweight PROPERTIES DEPENDS test_fcl_package_install)
add_test(
   NAME test_fcl_package_api_lightweight
   COMMAND ${CMAKE_CTEST_COMMAND}
      --build-and-test
      ${CMAKE_CURRENT_SOURCE_DIR}/package_api_lightweight
      ${CMAKE_BINARY_DIR}/fcl-package-api-lightweight
      --build-generator ${CMAKE_GENERATOR}
      --build-options
         ${FCL_PACKAGE_CONSUMER_BUILD_OPTIONS}
         -DCMAKE_DISABLE_FIND_PACKAGE_OpenSSL=ON
         -DCMAKE_DISABLE_FIND_PACKAGE_Ngtcp2=ON
         -DCMAKE_DISABLE_FIND_PACKAGE_glaze=ON
         -DCMAKE_DISABLE_FIND_PACKAGE_Notcurses=ON
      --test-command fcl_package_api_lightweight
)
set_tests_properties(test_fcl_package_api_lightweight PROPERTIES DEPENDS test_fcl_package_install)
add_test(
   NAME test_fcl_package_quic_component
   COMMAND ${CMAKE_CTEST_COMMAND}
      --build-and-test
      ${CMAKE_CURRENT_SOURCE_DIR}/package_quic_component
      ${CMAKE_BINARY_DIR}/fcl-package-quic-component
      --build-generator ${CMAKE_GENERATOR}
      --build-options ${FCL_PACKAGE_CONSUMER_BUILD_OPTIONS}
      --test-command fcl_package_quic_component
)
set_tests_properties(test_fcl_package_quic_component PROPERTIES DEPENDS test_fcl_package_install)
add_test(
   NAME test_fcl_package_all_missing_dependency
   COMMAND ${CMAKE_COMMAND}
      -DFCL_PACKAGE_TEST_SOURCE_DIR=${CMAKE_CURRENT_SOURCE_DIR}/package_all_missing_dependency
      -DFCL_PACKAGE_TEST_BINARY_DIR=${CMAKE_BINARY_DIR}/fcl-package-all-missing-dependency
      -DFCL_PACKAGE_TEST_PREFIX=${FCL_PACKAGE_INSTALL_PREFIX}
      -DFCL_PACKAGE_TEST_GENERATOR=${CMAKE_GENERATOR}
      -DFCL_PACKAGE_TEST_CXX_COMPILER=${CMAKE_CXX_COMPILER}
      -DFCL_PACKAGE_TEST_C_COMPILER=${CMAKE_C_COMPILER}
      -DFCL_PACKAGE_TEST_OSX_SYSROOT=${CMAKE_OSX_SYSROOT}
      -P ${CMAKE_CURRENT_SOURCE_DIR}/package_all_missing_dependency/expect_configure_failure.cmake
)
set_tests_properties(test_fcl_package_all_missing_dependency PROPERTIES DEPENDS test_fcl_package_install)
