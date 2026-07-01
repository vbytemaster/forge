find_package(Python3 REQUIRED COMPONENTS Interpreter)

add_executable(
   test_forge
   unit/main.cpp
   core/test_chrono.cpp
   core/test_encoding.cpp
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

target_link_libraries(test_forge forge Boost::included_unit_test_framework)
add_test(NAME test_forge COMMAND test_forge WORKING_DIRECTORY ${CMAKE_BINARY_DIR})

add_executable(
   test_forge_exceptions
   unit/main.cpp
   exceptions/exception_tests.cpp
)
target_link_libraries(test_forge_exceptions forge Boost::included_unit_test_framework)
add_test(NAME test_forge_exceptions COMMAND test_forge_exceptions WORKING_DIRECTORY ${CMAKE_BINARY_DIR})

add_executable(
   test_forge_log
   unit/main.cpp
   log/log_tests.cpp
)
target_link_libraries(test_forge_log forge Boost::included_unit_test_framework)
add_test(NAME test_forge_log COMMAND test_forge_log WORKING_DIRECTORY ${CMAKE_BINARY_DIR})

add_executable(
   test_forge_otlp
   unit/main.cpp
   otlp/otlp_tests.cpp
)
target_link_libraries(test_forge_otlp forge Boost::included_unit_test_framework)
add_executable(forge_otlp_crash_helper otlp/crash_helper.cpp)
target_link_libraries(forge_otlp_crash_helper forge)
add_dependencies(test_forge_otlp forge_otlp_crash_helper)
target_compile_definitions(test_forge_otlp PRIVATE FORGE_OTLP_CRASH_HELPER="$<TARGET_FILE:forge_otlp_crash_helper>")
add_test(NAME test_forge_otlp COMMAND test_forge_otlp WORKING_DIRECTORY ${CMAKE_BINARY_DIR})

add_executable(
   test_forge_json
   unit/main.cpp
   json/test_json.cpp
)
target_link_libraries(test_forge_json forge Boost::included_unit_test_framework)
add_test(NAME test_forge_json COMMAND test_forge_json WORKING_DIRECTORY ${CMAKE_BINARY_DIR})

add_executable(
   test_forge_raw
   unit/main.cpp
   raw/test_raw.cpp
)
target_link_libraries(test_forge_raw forge Boost::included_unit_test_framework)
add_test(NAME test_forge_raw COMMAND test_forge_raw WORKING_DIRECTORY ${CMAKE_BINARY_DIR})

add_executable(
   test_forge_ids
   unit/main.cpp
   ids/ids_tests.cpp
)
target_link_libraries(test_forge_ids forge_ids forge Boost::included_unit_test_framework)
add_test(NAME test_forge_ids COMMAND test_forge_ids WORKING_DIRECTORY ${CMAKE_BINARY_DIR})

add_executable(
   test_forge_objectdb
   unit/main.cpp
   objectdb/objectdb_tests.cpp
)
target_link_libraries(test_forge_objectdb forge_objectdb forge Boost::included_unit_test_framework)
add_test(NAME test_forge_objectdb COMMAND test_forge_objectdb WORKING_DIRECTORY ${CMAKE_BINARY_DIR})

add_executable(
   test_forge_crypto
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
target_link_libraries(test_forge_crypto forge Boost::included_unit_test_framework)
add_test(NAME test_forge_crypto COMMAND test_forge_crypto WORKING_DIRECTORY ${CMAKE_BINARY_DIR})

add_executable(
   test_forge_multiformats
   unit/main.cpp
   multiformats/multiformats_tests.cpp
)
target_link_libraries(test_forge_multiformats forge Boost::included_unit_test_framework)
add_test(NAME test_forge_multiformats COMMAND test_forge_multiformats WORKING_DIRECTORY ${CMAKE_BINARY_DIR})

add_executable(
   test_forge_asio
   unit/main.cpp
   asio/task_scheduler_tests.cpp
)
target_link_libraries(test_forge_asio forge Boost::included_unit_test_framework)
add_test(NAME test_forge_asio COMMAND test_forge_asio WORKING_DIRECTORY ${CMAKE_BINARY_DIR})

add_executable(
   test_forge_transport
   unit/main.cpp
   transport/transport_tests.cpp
)
target_link_libraries(test_forge_transport forge Boost::included_unit_test_framework)
add_test(NAME test_forge_transport COMMAND test_forge_transport WORKING_DIRECTORY ${CMAKE_BINARY_DIR})

add_executable(
   test_forge_tcp
   unit/main.cpp
   tcp/tcp_tests.cpp
)
target_link_libraries(test_forge_tcp forge Boost::included_unit_test_framework)
add_test(NAME test_forge_tcp COMMAND test_forge_tcp WORKING_DIRECTORY ${CMAKE_BINARY_DIR})

add_executable(
   test_forge_stcp
   unit/main.cpp
   stcp/stcp_tests.cpp
)
target_link_libraries(test_forge_stcp forge Boost::included_unit_test_framework OpenSSL::SSL)
add_test(NAME test_forge_stcp COMMAND test_forge_stcp WORKING_DIRECTORY ${CMAKE_BINARY_DIR})

add_executable(
   test_forge_yamux
   unit/main.cpp
   yamux/yamux_tests.cpp
)
target_link_libraries(test_forge_yamux forge Boost::included_unit_test_framework)
add_test(NAME test_forge_yamux COMMAND test_forge_yamux WORKING_DIRECTORY ${CMAKE_BINARY_DIR})

add_executable(
   test_forge_quic
   unit/main.cpp
   quic/quic_transport_tests.cpp
)
target_link_libraries(test_forge_quic forge Boost::included_unit_test_framework)
add_test(NAME test_forge_quic COMMAND test_forge_quic WORKING_DIRECTORY ${CMAKE_BINARY_DIR})

add_executable(
   test_forge_app
   unit/main.cpp
   app/app_tests.cpp
)
target_link_libraries(test_forge_app forge Boost::included_unit_test_framework)
add_test(NAME test_forge_app COMMAND test_forge_app WORKING_DIRECTORY ${CMAKE_BINARY_DIR})

add_executable(
   test_forge_schema
   unit/main.cpp
   schema/schema_tests.cpp
)
target_link_libraries(test_forge_schema forge Boost::included_unit_test_framework)
add_test(NAME test_forge_schema COMMAND test_forge_schema WORKING_DIRECTORY ${CMAKE_BINARY_DIR})

add_executable(
   test_forge_config
   unit/main.cpp
   config/config_tests.cpp
)
target_link_libraries(test_forge_config forge Boost::included_unit_test_framework)
add_test(NAME test_forge_config COMMAND test_forge_config WORKING_DIRECTORY ${CMAKE_BINARY_DIR})

add_executable(
   test_forge_yaml
   unit/main.cpp
   yaml/yaml_tests.cpp
)
target_link_libraries(test_forge_yaml forge Boost::included_unit_test_framework)
add_test(NAME test_forge_yaml COMMAND test_forge_yaml WORKING_DIRECTORY ${CMAKE_BINARY_DIR})

add_executable(
   test_forge_xml
   unit/main.cpp
   xml/xml_tests.cpp
)
target_link_libraries(test_forge_xml forge_xml Boost::included_unit_test_framework)
add_test(NAME test_forge_xml COMMAND test_forge_xml WORKING_DIRECTORY ${CMAKE_BINARY_DIR})

add_executable(
   test_forge_program_options
   unit/main.cpp
   program_options/program_options_tests.cpp
)
target_link_libraries(test_forge_program_options forge Boost::included_unit_test_framework)
add_test(NAME test_forge_program_options COMMAND test_forge_program_options WORKING_DIRECTORY ${CMAKE_BINARY_DIR})

add_executable(
   test_forge_env
   unit/main.cpp
   env/env_tests.cpp
)
target_link_libraries(test_forge_env forge Boost::included_unit_test_framework)
add_test(NAME test_forge_env COMMAND test_forge_env WORKING_DIRECTORY ${CMAKE_BINARY_DIR})

add_executable(
   test_forge_api
   unit/main.cpp
   api/api_tests.cpp
   api/api_module_smoke_tests.cpp
)
target_link_libraries(test_forge_api forge Boost::included_unit_test_framework)
add_test(NAME test_forge_api COMMAND test_forge_api WORKING_DIRECTORY ${CMAKE_BINARY_DIR})

add_executable(
   test_forge_transport_api
   unit/main.cpp
   api/transport_api_tests.cpp
)
target_link_libraries(test_forge_transport_api forge Boost::included_unit_test_framework)
add_test(NAME test_forge_transport_api COMMAND test_forge_transport_api WORKING_DIRECTORY ${CMAKE_BINARY_DIR})

add_executable(
   test_forge_http_websocket
   unit/main.cpp
   http_websocket/network_tests.cpp
)
target_link_libraries(test_forge_http_websocket forge Boost::included_unit_test_framework)
add_test(NAME test_forge_http_websocket COMMAND test_forge_http_websocket WORKING_DIRECTORY ${CMAKE_BINARY_DIR})

add_executable(
   test_forge_quic_p2p
   unit/main.cpp
   quic_p2p/quic_tests.cpp
   quic_p2p/p2p_tests.cpp
)
target_link_libraries(test_forge_quic_p2p forge Boost::included_unit_test_framework)
target_compile_definitions(test_forge_quic_p2p PRIVATE FORGE_HAS_ROCKSDB=$<BOOL:${FORGE_HAS_ROCKSDB}>)
add_test(NAME test_forge_quic_p2p COMMAND test_forge_quic_p2p WORKING_DIRECTORY ${CMAKE_BINARY_DIR})

add_executable(
   forge_interop_fixture
   libp2p_interop/forge_interop_fixture.cpp
)
target_link_libraries(forge_interop_fixture forge)

set(_forge_libp2p_interop_enabled 0)
if(FORGE_ENABLE_LIBP2P_INTEROP)
   set(_forge_libp2p_interop_enabled 1)
endif()

add_custom_target(
   test_forge_libp2p_interop
   COMMAND ${Python3_EXECUTABLE}
      ${CMAKE_CURRENT_SOURCE_DIR}/libp2p_interop/runner.py
      --enabled ${_forge_libp2p_interop_enabled}
      --forge-fixture $<TARGET_FILE:forge_interop_fixture>
      --source-dir ${CMAKE_CURRENT_SOURCE_DIR}/libp2p_interop
      --build-dir ${CMAKE_CURRENT_BINARY_DIR}/libp2p_interop
      --donors-root ${FORGE_LIBP2P_DONORS_ROOT}
   DEPENDS forge_interop_fixture
)
add_test(
   NAME test_forge_libp2p_interop
   COMMAND ${Python3_EXECUTABLE}
      ${CMAKE_CURRENT_SOURCE_DIR}/libp2p_interop/runner.py
      --enabled ${_forge_libp2p_interop_enabled}
      --forge-fixture $<TARGET_FILE:forge_interop_fixture>
      --source-dir ${CMAKE_CURRENT_SOURCE_DIR}/libp2p_interop
      --build-dir ${CMAKE_CURRENT_BINARY_DIR}/libp2p_interop
      --donors-root ${FORGE_LIBP2P_DONORS_ROOT}
)
set_tests_properties(test_forge_libp2p_interop PROPERTIES SKIP_REGULAR_EXPRESSION "^SKIP:")

add_test(
   NAME test_forge_libp2p_donor_matrix
   COMMAND ${Python3_EXECUTABLE}
      ${CMAKE_CURRENT_SOURCE_DIR}/libp2p_interop/check_donor_matrix.py
      ${CMAKE_CURRENT_SOURCE_DIR}/libp2p_interop/donor_cases.json
)

add_executable(
   test_forge_plugins
   unit/main.cpp
   plugins/plugins_tests.cpp
   plugins/log_otlp_tests.cpp
)
target_link_libraries(test_forge_plugins forge Boost::included_unit_test_framework)
add_test(NAME test_forge_plugins COMMAND test_forge_plugins WORKING_DIRECTORY ${CMAKE_BINARY_DIR})

add_executable(
   test_forge_crypto_secrets
   unit/main.cpp
   plugins/crypto/secrets_tests.cpp
)
target_link_libraries(test_forge_crypto_secrets forge Boost::included_unit_test_framework)
target_include_directories(test_forge_crypto_secrets PRIVATE ${PROJECT_SOURCE_DIR}/plugins/crypto/secrets)
add_test(NAME test_forge_crypto_secrets COMMAND test_forge_crypto_secrets WORKING_DIRECTORY ${CMAKE_BINARY_DIR})

if(FORGE_HAS_ROCKSDB)
   add_executable(
      test_forge_rocksdb
      unit/main.cpp
      rocksdb/rocksdb_tests.cpp
   )
   target_link_libraries(test_forge_rocksdb forge_rocksdb forge Boost::included_unit_test_framework)
   add_test(NAME test_forge_rocksdb COMMAND test_forge_rocksdb WORKING_DIRECTORY ${CMAKE_BINARY_DIR})

   add_executable(
      test_forge_plugins_db_rocksdb
      unit/main.cpp
      plugins_db_rocksdb/rocksdb_tests.cpp
   )
   target_link_libraries(test_forge_plugins_db_rocksdb forge_plugins_db_rocksdb forge Boost::included_unit_test_framework)
   add_test(NAME test_forge_plugins_db_rocksdb COMMAND test_forge_plugins_db_rocksdb WORKING_DIRECTORY ${CMAKE_BINARY_DIR})
else()
   message(STATUS "Skipping test_forge_rocksdb: RocksDB is not available")
   message(STATUS "Skipping test_forge_plugins_db_rocksdb: RocksDB is not available")
endif()

if(TARGET forge_tui)
   add_executable(
      test_forge_tui
      unit/main.cpp
      tui/tui_tests.cpp
   )
   target_link_libraries(test_forge_tui forge forge_tui Boost::included_unit_test_framework)
   add_test(NAME test_forge_tui COMMAND test_forge_tui WORKING_DIRECTORY ${CMAKE_BINARY_DIR})
else()
   message(STATUS "Skipping test_forge_tui: forge_tui is not available")
endif()

set(FORGE_PACKAGE_INSTALL_PREFIX "${CMAKE_BINARY_DIR}/forge-package-install")
set(
   FORGE_PACKAGE_CONSUMER_BUILD_OPTIONS
   -DCMAKE_PREFIX_PATH=${FORGE_PACKAGE_INSTALL_PREFIX}
   -DCMAKE_CXX_COMPILER=${CMAKE_CXX_COMPILER}
   -DCMAKE_C_COMPILER=${CMAKE_C_COMPILER}
   -DCMAKE_OSX_SYSROOT=${CMAKE_OSX_SYSROOT}
)

add_custom_target(
   test_forge_package_install
   COMMAND ${CMAKE_COMMAND} --install ${CMAKE_BINARY_DIR} --prefix ${FORGE_PACKAGE_INSTALL_PREFIX} --component dev
   DEPENDS forge
)
add_custom_target(
   test_forge_package_consumer
   COMMAND ${CMAKE_CTEST_COMMAND}
      --build-and-test
      ${CMAKE_CURRENT_SOURCE_DIR}/package_consumer
      ${CMAKE_BINARY_DIR}/forge-package-consumer
      --build-generator ${CMAKE_GENERATOR}
      --build-options ${FORGE_PACKAGE_CONSUMER_BUILD_OPTIONS}
      --test-command forge_package_consumer
   DEPENDS test_forge_package_install
)
add_custom_target(
   test_forge_package_core_lightweight
   COMMAND ${CMAKE_CTEST_COMMAND}
      --build-and-test
      ${CMAKE_CURRENT_SOURCE_DIR}/package_core_lightweight
      ${CMAKE_BINARY_DIR}/forge-package-core-lightweight
      --build-generator ${CMAKE_GENERATOR}
      --build-options
         ${FORGE_PACKAGE_CONSUMER_BUILD_OPTIONS}
         -DCMAKE_DISABLE_FIND_PACKAGE_OpenSSL=ON
         -DCMAKE_DISABLE_FIND_PACKAGE_Ngtcp2=ON
         -DCMAKE_DISABLE_FIND_PACKAGE_glaze=ON
         -DCMAKE_DISABLE_FIND_PACKAGE_Notcurses=ON
      --test-command forge_package_core_lightweight
   DEPENDS test_forge_package_install
)
add_custom_target(
   test_forge_package_env_lightweight
   COMMAND ${CMAKE_CTEST_COMMAND}
      --build-and-test
      ${CMAKE_CURRENT_SOURCE_DIR}/package_env_lightweight
      ${CMAKE_BINARY_DIR}/forge-package-env-lightweight
      --build-generator ${CMAKE_GENERATOR}
      --build-options
         ${FORGE_PACKAGE_CONSUMER_BUILD_OPTIONS}
         -DCMAKE_DISABLE_FIND_PACKAGE_OpenSSL=ON
         -DCMAKE_DISABLE_FIND_PACKAGE_Ngtcp2=ON
         -DCMAKE_DISABLE_FIND_PACKAGE_glaze=ON
         -DCMAKE_DISABLE_FIND_PACKAGE_Notcurses=ON
      --test-command forge_package_env_lightweight
   DEPENDS test_forge_package_install
)
add_custom_target(
   test_forge_package_api_lightweight
   COMMAND ${CMAKE_CTEST_COMMAND}
      --build-and-test
      ${CMAKE_CURRENT_SOURCE_DIR}/package_api_lightweight
      ${CMAKE_BINARY_DIR}/forge-package-api-lightweight
      --build-generator ${CMAKE_GENERATOR}
      --build-options
         ${FORGE_PACKAGE_CONSUMER_BUILD_OPTIONS}
         -DCMAKE_DISABLE_FIND_PACKAGE_OpenSSL=ON
         -DCMAKE_DISABLE_FIND_PACKAGE_Ngtcp2=ON
         -DCMAKE_DISABLE_FIND_PACKAGE_glaze=ON
         -DCMAKE_DISABLE_FIND_PACKAGE_Notcurses=ON
      --test-command forge_package_api_lightweight
   DEPENDS test_forge_package_install
)
add_custom_target(
   test_forge_package_quic_component
   COMMAND ${CMAKE_CTEST_COMMAND}
      --build-and-test
      ${CMAKE_CURRENT_SOURCE_DIR}/package_quic_component
      ${CMAKE_BINARY_DIR}/forge-package-quic-component
      --build-generator ${CMAKE_GENERATOR}
      --build-options ${FORGE_PACKAGE_CONSUMER_BUILD_OPTIONS}
      --test-command forge_package_quic_component
   DEPENDS test_forge_package_install
)
add_custom_target(
   test_forge_package_otlp_component
   COMMAND ${CMAKE_CTEST_COMMAND}
      --build-and-test
      ${CMAKE_CURRENT_SOURCE_DIR}/package_otlp_component
      ${CMAKE_BINARY_DIR}/forge-package-otlp-component
      --build-generator ${CMAKE_GENERATOR}
      --build-options ${FORGE_PACKAGE_CONSUMER_BUILD_OPTIONS}
      --test-command forge_package_otlp_component
   DEPENDS test_forge_package_install
)
add_custom_target(
   test_forge_package_http_api_component
   COMMAND ${CMAKE_CTEST_COMMAND}
      --build-and-test
      ${CMAKE_CURRENT_SOURCE_DIR}/package_http_api_component
      ${CMAKE_BINARY_DIR}/forge-package-http-api-component
      --build-generator ${CMAKE_GENERATOR}
      --build-options ${FORGE_PACKAGE_CONSUMER_BUILD_OPTIONS}
      --test-command forge_package_http_api_component
   DEPENDS test_forge_package_install
)
add_custom_target(
   test_forge_package_ids_component
   COMMAND ${CMAKE_CTEST_COMMAND}
      --build-and-test
      ${CMAKE_CURRENT_SOURCE_DIR}/package_ids_component
      ${CMAKE_BINARY_DIR}/forge-package-ids-component
      --build-generator ${CMAKE_GENERATOR}
      --build-options ${FORGE_PACKAGE_CONSUMER_BUILD_OPTIONS}
      --test-command forge_package_ids_component
   DEPENDS test_forge_package_install
)
add_custom_target(
   test_forge_package_objectdb_component
   COMMAND ${CMAKE_CTEST_COMMAND}
      --build-and-test
      ${CMAKE_CURRENT_SOURCE_DIR}/package_objectdb_component
      ${CMAKE_BINARY_DIR}/forge-package-objectdb-component
      --build-generator ${CMAKE_GENERATOR}
      --build-options ${FORGE_PACKAGE_CONSUMER_BUILD_OPTIONS}
      --test-command forge_package_objectdb_component
   DEPENDS test_forge_package_install
)
add_custom_target(
   test_forge_package_xml_component
   COMMAND ${CMAKE_CTEST_COMMAND}
      --build-and-test
      ${CMAKE_CURRENT_SOURCE_DIR}/package_xml_component
      ${CMAKE_BINARY_DIR}/forge-package-xml-component
      --build-generator ${CMAKE_GENERATOR}
      --build-options
         ${FORGE_PACKAGE_CONSUMER_BUILD_OPTIONS}
         -DCMAKE_DISABLE_FIND_PACKAGE_glaze=ON
      --test-command forge_package_xml_component
   DEPENDS test_forge_package_install
)
add_custom_target(
   test_forge_package_xml_add_subdirectory
   COMMAND ${CMAKE_CTEST_COMMAND}
      --build-and-test
      ${CMAKE_CURRENT_SOURCE_DIR}/package_xml_add_subdirectory
      ${CMAKE_BINARY_DIR}/forge-package-xml-add-subdirectory
      --build-generator ${CMAKE_GENERATOR}
      --build-options
         ${FORGE_PACKAGE_CONSUMER_BUILD_OPTIONS}
         -DFORGE_SOURCE_DIR=${CMAKE_SOURCE_DIR}
      --build-target forge_xml_add_subdirectory
      --test-command forge_xml_add_subdirectory
)
add_custom_target(
   test_forge_package_transport_api_component
   COMMAND ${CMAKE_CTEST_COMMAND}
      --build-and-test
      ${CMAKE_CURRENT_SOURCE_DIR}/package_transport_api_component
      ${CMAKE_BINARY_DIR}/forge-package-transport-api-component
      --build-generator ${CMAKE_GENERATOR}
      --build-options ${FORGE_PACKAGE_CONSUMER_BUILD_OPTIONS}
      --test-command forge_package_transport_api_component
   DEPENDS test_forge_package_install
)
add_custom_target(
   test_forge_package_plugins_p2p_node
   COMMAND ${CMAKE_CTEST_COMMAND}
      --build-and-test
      ${CMAKE_CURRENT_SOURCE_DIR}/package_plugins_p2p_node
      ${CMAKE_BINARY_DIR}/forge-package-plugins-p2p-node
      --build-generator ${CMAKE_GENERATOR}
      --build-options ${FORGE_PACKAGE_CONSUMER_BUILD_OPTIONS}
      --test-command forge_package_plugins_p2p_node
   DEPENDS test_forge_package_install
)
add_custom_target(
   test_forge_package_plugins_crypto_signer
   COMMAND ${CMAKE_CTEST_COMMAND}
      --build-and-test
      ${CMAKE_CURRENT_SOURCE_DIR}/package_plugins_crypto_signer
      ${CMAKE_BINARY_DIR}/forge-package-plugins-crypto-signer
      --build-generator ${CMAKE_GENERATOR}
      --build-options ${FORGE_PACKAGE_CONSUMER_BUILD_OPTIONS}
      --test-command forge_package_plugins_crypto_signer
   DEPENDS test_forge_package_install
)
add_custom_target(
   test_forge_package_plugins_crypto_secrets
   COMMAND ${CMAKE_CTEST_COMMAND}
      --build-and-test
      ${CMAKE_CURRENT_SOURCE_DIR}/package_plugins_crypto_secrets
      ${CMAKE_BINARY_DIR}/forge-package-plugins-crypto-secrets
      --build-generator ${CMAKE_GENERATOR}
      --build-options ${FORGE_PACKAGE_CONSUMER_BUILD_OPTIONS}
      --test-command forge_package_plugins_crypto_secrets
   DEPENDS test_forge_package_install
)
add_custom_target(
   test_forge_package_plugins_http_server
   COMMAND ${CMAKE_CTEST_COMMAND}
      --build-and-test
      ${CMAKE_CURRENT_SOURCE_DIR}/package_plugins_http_server
      ${CMAKE_BINARY_DIR}/forge-package-plugins-http-server
      --build-generator ${CMAKE_GENERATOR}
      --build-options ${FORGE_PACKAGE_CONSUMER_BUILD_OPTIONS}
      --test-command forge_package_plugins_http_server
   DEPENDS test_forge_package_install
)
add_custom_target(
   test_forge_package_plugins_log_otlp
   COMMAND ${CMAKE_CTEST_COMMAND}
      --build-and-test
      ${CMAKE_CURRENT_SOURCE_DIR}/package_plugins_log_otlp
      ${CMAKE_BINARY_DIR}/forge-package-plugins-log-otlp
      --build-generator ${CMAKE_GENERATOR}
      --build-options ${FORGE_PACKAGE_CONSUMER_BUILD_OPTIONS}
      --test-command forge_package_plugins_log_otlp
   DEPENDS test_forge_package_install
)
if(FORGE_HAS_ROCKSDB)
   add_custom_target(
      test_forge_package_rocksdb_component
      COMMAND ${CMAKE_CTEST_COMMAND}
         --build-and-test
         ${CMAKE_CURRENT_SOURCE_DIR}/package_rocksdb_component
         ${CMAKE_BINARY_DIR}/forge-package-rocksdb-component
         --build-generator ${CMAKE_GENERATOR}
         --build-options ${FORGE_PACKAGE_CONSUMER_BUILD_OPTIONS}
         --test-command forge_package_rocksdb_component
      DEPENDS test_forge_package_install
   )

   add_custom_target(
      test_forge_package_plugins_db_rocksdb
      COMMAND ${CMAKE_CTEST_COMMAND}
         --build-and-test
         ${CMAKE_CURRENT_SOURCE_DIR}/package_plugins_db_rocksdb
         ${CMAKE_BINARY_DIR}/forge-package-plugins-db-rocksdb
         --build-generator ${CMAKE_GENERATOR}
         --build-options ${FORGE_PACKAGE_CONSUMER_BUILD_OPTIONS}
         --test-command forge_package_plugins_db_rocksdb
      DEPENDS test_forge_package_install
   )
else()
   add_custom_target(
      test_forge_package_rocksdb_component
      COMMAND ${CMAKE_COMMAND} -E echo "Skipping test_forge_package_rocksdb_component: RocksDB is not available"
   )
   add_custom_target(
      test_forge_package_plugins_db_rocksdb
      COMMAND ${CMAKE_COMMAND} -E echo "Skipping test_forge_package_plugins_db_rocksdb: RocksDB is not available"
   )
endif()
add_custom_target(
   test_forge_package_plugins_component
   COMMAND ${CMAKE_CTEST_COMMAND}
      --build-and-test
      ${CMAKE_CURRENT_SOURCE_DIR}/package_plugins_component
      ${CMAKE_BINARY_DIR}/forge-package-plugins-component
      --build-generator ${CMAKE_GENERATOR}
      --build-options ${FORGE_PACKAGE_CONSUMER_BUILD_OPTIONS}
      --test-command forge_package_plugins_component
   DEPENDS test_forge_package_install
)
add_custom_target(
   test_forge_package_all_missing_dependency
   COMMAND ${CMAKE_COMMAND}
      -DFORGE_PACKAGE_TEST_SOURCE_DIR=${CMAKE_CURRENT_SOURCE_DIR}/package_all_missing_dependency
      -DFORGE_PACKAGE_TEST_BINARY_DIR=${CMAKE_BINARY_DIR}/forge-package-all-missing-dependency
      -DFORGE_PACKAGE_TEST_PREFIX=${FORGE_PACKAGE_INSTALL_PREFIX}
      -DFORGE_PACKAGE_TEST_GENERATOR=${CMAKE_GENERATOR}
      -DFORGE_PACKAGE_TEST_CXX_COMPILER=${CMAKE_CXX_COMPILER}
      -DFORGE_PACKAGE_TEST_C_COMPILER=${CMAKE_C_COMPILER}
      -DFORGE_PACKAGE_TEST_OSX_SYSROOT=${CMAKE_OSX_SYSROOT}
      -P ${CMAKE_CURRENT_SOURCE_DIR}/package_all_missing_dependency/expect_configure_failure.cmake
   DEPENDS test_forge_package_install
)

add_test(
   NAME test_forge_package_install
   COMMAND ${CMAKE_COMMAND} --install ${CMAKE_BINARY_DIR} --prefix ${FORGE_PACKAGE_INSTALL_PREFIX} --component dev
)
add_test(
   NAME test_forge_package_consumer
   COMMAND ${CMAKE_CTEST_COMMAND}
      --build-and-test
      ${CMAKE_CURRENT_SOURCE_DIR}/package_consumer
      ${CMAKE_BINARY_DIR}/forge-package-consumer
      --build-generator ${CMAKE_GENERATOR}
      --build-options ${FORGE_PACKAGE_CONSUMER_BUILD_OPTIONS}
      --test-command forge_package_consumer
)
set_tests_properties(test_forge_package_consumer PROPERTIES DEPENDS test_forge_package_install)
add_test(
   NAME test_forge_package_core_lightweight
   COMMAND ${CMAKE_CTEST_COMMAND}
      --build-and-test
      ${CMAKE_CURRENT_SOURCE_DIR}/package_core_lightweight
      ${CMAKE_BINARY_DIR}/forge-package-core-lightweight
      --build-generator ${CMAKE_GENERATOR}
      --build-options
         ${FORGE_PACKAGE_CONSUMER_BUILD_OPTIONS}
         -DCMAKE_DISABLE_FIND_PACKAGE_OpenSSL=ON
         -DCMAKE_DISABLE_FIND_PACKAGE_Ngtcp2=ON
         -DCMAKE_DISABLE_FIND_PACKAGE_glaze=ON
         -DCMAKE_DISABLE_FIND_PACKAGE_Notcurses=ON
      --test-command forge_package_core_lightweight
)
set_tests_properties(test_forge_package_core_lightweight PROPERTIES DEPENDS test_forge_package_install)
add_test(
   NAME test_forge_package_env_lightweight
   COMMAND ${CMAKE_CTEST_COMMAND}
      --build-and-test
      ${CMAKE_CURRENT_SOURCE_DIR}/package_env_lightweight
      ${CMAKE_BINARY_DIR}/forge-package-env-lightweight
      --build-generator ${CMAKE_GENERATOR}
      --build-options
         ${FORGE_PACKAGE_CONSUMER_BUILD_OPTIONS}
         -DCMAKE_DISABLE_FIND_PACKAGE_OpenSSL=ON
         -DCMAKE_DISABLE_FIND_PACKAGE_Ngtcp2=ON
         -DCMAKE_DISABLE_FIND_PACKAGE_glaze=ON
         -DCMAKE_DISABLE_FIND_PACKAGE_Notcurses=ON
      --test-command forge_package_env_lightweight
)
set_tests_properties(test_forge_package_env_lightweight PROPERTIES DEPENDS test_forge_package_install)
add_test(
   NAME test_forge_package_api_lightweight
   COMMAND ${CMAKE_CTEST_COMMAND}
      --build-and-test
      ${CMAKE_CURRENT_SOURCE_DIR}/package_api_lightweight
      ${CMAKE_BINARY_DIR}/forge-package-api-lightweight
      --build-generator ${CMAKE_GENERATOR}
      --build-options
         ${FORGE_PACKAGE_CONSUMER_BUILD_OPTIONS}
         -DCMAKE_DISABLE_FIND_PACKAGE_OpenSSL=ON
         -DCMAKE_DISABLE_FIND_PACKAGE_Ngtcp2=ON
         -DCMAKE_DISABLE_FIND_PACKAGE_glaze=ON
         -DCMAKE_DISABLE_FIND_PACKAGE_Notcurses=ON
      --test-command forge_package_api_lightweight
)
set_tests_properties(test_forge_package_api_lightweight PROPERTIES DEPENDS test_forge_package_install)
add_test(
   NAME test_forge_package_quic_component
   COMMAND ${CMAKE_CTEST_COMMAND}
      --build-and-test
      ${CMAKE_CURRENT_SOURCE_DIR}/package_quic_component
      ${CMAKE_BINARY_DIR}/forge-package-quic-component
      --build-generator ${CMAKE_GENERATOR}
      --build-options ${FORGE_PACKAGE_CONSUMER_BUILD_OPTIONS}
      --test-command forge_package_quic_component
)
set_tests_properties(test_forge_package_quic_component PROPERTIES DEPENDS test_forge_package_install)
add_test(
   NAME test_forge_package_otlp_component
   COMMAND ${CMAKE_CTEST_COMMAND}
      --build-and-test
      ${CMAKE_CURRENT_SOURCE_DIR}/package_otlp_component
      ${CMAKE_BINARY_DIR}/forge-package-otlp-component
      --build-generator ${CMAKE_GENERATOR}
      --build-options ${FORGE_PACKAGE_CONSUMER_BUILD_OPTIONS}
      --test-command forge_package_otlp_component
)
set_tests_properties(test_forge_package_otlp_component PROPERTIES DEPENDS test_forge_package_install)
add_test(
   NAME test_forge_package_http_api_component
   COMMAND ${CMAKE_CTEST_COMMAND}
      --build-and-test
      ${CMAKE_CURRENT_SOURCE_DIR}/package_http_api_component
      ${CMAKE_BINARY_DIR}/forge-package-http-api-component
      --build-generator ${CMAKE_GENERATOR}
      --build-options ${FORGE_PACKAGE_CONSUMER_BUILD_OPTIONS}
      --test-command forge_package_http_api_component
)
set_tests_properties(test_forge_package_http_api_component PROPERTIES DEPENDS test_forge_package_install)
add_test(
   NAME test_forge_package_ids_component
   COMMAND ${CMAKE_CTEST_COMMAND}
      --build-and-test
      ${CMAKE_CURRENT_SOURCE_DIR}/package_ids_component
      ${CMAKE_BINARY_DIR}/forge-package-ids-component
      --build-generator ${CMAKE_GENERATOR}
      --build-options ${FORGE_PACKAGE_CONSUMER_BUILD_OPTIONS}
      --test-command forge_package_ids_component
)
set_tests_properties(test_forge_package_ids_component PROPERTIES DEPENDS test_forge_package_install)
add_test(
   NAME test_forge_package_objectdb_component
   COMMAND ${CMAKE_CTEST_COMMAND}
      --build-and-test
      ${CMAKE_CURRENT_SOURCE_DIR}/package_objectdb_component
      ${CMAKE_BINARY_DIR}/forge-package-objectdb-component
      --build-generator ${CMAKE_GENERATOR}
      --build-options ${FORGE_PACKAGE_CONSUMER_BUILD_OPTIONS}
      --test-command forge_package_objectdb_component
)
set_tests_properties(test_forge_package_objectdb_component PROPERTIES DEPENDS test_forge_package_install)
add_test(
   NAME test_forge_package_xml_component
   COMMAND ${CMAKE_CTEST_COMMAND}
      --build-and-test
      ${CMAKE_CURRENT_SOURCE_DIR}/package_xml_component
      ${CMAKE_BINARY_DIR}/forge-package-xml-component
      --build-generator ${CMAKE_GENERATOR}
      --build-options
         ${FORGE_PACKAGE_CONSUMER_BUILD_OPTIONS}
         -DCMAKE_DISABLE_FIND_PACKAGE_glaze=ON
      --test-command forge_package_xml_component
)
set_tests_properties(test_forge_package_xml_component PROPERTIES DEPENDS test_forge_package_install)
add_test(
   NAME test_forge_package_xml_add_subdirectory
   COMMAND ${CMAKE_CTEST_COMMAND}
      --build-and-test
      ${CMAKE_CURRENT_SOURCE_DIR}/package_xml_add_subdirectory
      ${CMAKE_BINARY_DIR}/forge-package-xml-add-subdirectory
      --build-generator ${CMAKE_GENERATOR}
      --build-options
         ${FORGE_PACKAGE_CONSUMER_BUILD_OPTIONS}
         -DFORGE_SOURCE_DIR=${CMAKE_SOURCE_DIR}
      --build-target forge_xml_add_subdirectory
      --test-command forge_xml_add_subdirectory
)
add_test(
   NAME test_forge_package_transport_api_component
   COMMAND ${CMAKE_CTEST_COMMAND}
      --build-and-test
      ${CMAKE_CURRENT_SOURCE_DIR}/package_transport_api_component
      ${CMAKE_BINARY_DIR}/forge-package-transport-api-component
      --build-generator ${CMAKE_GENERATOR}
      --build-options ${FORGE_PACKAGE_CONSUMER_BUILD_OPTIONS}
      --test-command forge_package_transport_api_component
)
set_tests_properties(test_forge_package_transport_api_component PROPERTIES DEPENDS test_forge_package_install)
add_test(
   NAME test_forge_package_plugins_p2p_node
   COMMAND ${CMAKE_CTEST_COMMAND}
      --build-and-test
      ${CMAKE_CURRENT_SOURCE_DIR}/package_plugins_p2p_node
      ${CMAKE_BINARY_DIR}/forge-package-plugins-p2p-node
      --build-generator ${CMAKE_GENERATOR}
      --build-options ${FORGE_PACKAGE_CONSUMER_BUILD_OPTIONS}
      --test-command forge_package_plugins_p2p_node
)
set_tests_properties(test_forge_package_plugins_p2p_node PROPERTIES DEPENDS test_forge_package_install)
add_test(
   NAME test_forge_package_plugins_crypto_signer
   COMMAND ${CMAKE_CTEST_COMMAND}
      --build-and-test
      ${CMAKE_CURRENT_SOURCE_DIR}/package_plugins_crypto_signer
      ${CMAKE_BINARY_DIR}/forge-package-plugins-crypto-signer
      --build-generator ${CMAKE_GENERATOR}
      --build-options ${FORGE_PACKAGE_CONSUMER_BUILD_OPTIONS}
      --test-command forge_package_plugins_crypto_signer
)
set_tests_properties(test_forge_package_plugins_crypto_signer PROPERTIES DEPENDS test_forge_package_install)
add_test(
   NAME test_forge_package_plugins_crypto_secrets
   COMMAND ${CMAKE_CTEST_COMMAND}
      --build-and-test
      ${CMAKE_CURRENT_SOURCE_DIR}/package_plugins_crypto_secrets
      ${CMAKE_BINARY_DIR}/forge-package-plugins-crypto-secrets
      --build-generator ${CMAKE_GENERATOR}
      --build-options ${FORGE_PACKAGE_CONSUMER_BUILD_OPTIONS}
      --test-command forge_package_plugins_crypto_secrets
)
set_tests_properties(test_forge_package_plugins_crypto_secrets PROPERTIES DEPENDS test_forge_package_install)
add_test(
   NAME test_forge_package_plugins_http_server
   COMMAND ${CMAKE_CTEST_COMMAND}
      --build-and-test
      ${CMAKE_CURRENT_SOURCE_DIR}/package_plugins_http_server
      ${CMAKE_BINARY_DIR}/forge-package-plugins-http-server
      --build-generator ${CMAKE_GENERATOR}
      --build-options ${FORGE_PACKAGE_CONSUMER_BUILD_OPTIONS}
      --test-command forge_package_plugins_http_server
)
set_tests_properties(test_forge_package_plugins_http_server PROPERTIES DEPENDS test_forge_package_install)
add_test(
   NAME test_forge_package_plugins_log_otlp
   COMMAND ${CMAKE_CTEST_COMMAND}
      --build-and-test
      ${CMAKE_CURRENT_SOURCE_DIR}/package_plugins_log_otlp
      ${CMAKE_BINARY_DIR}/forge-package-plugins-log-otlp
      --build-generator ${CMAKE_GENERATOR}
      --build-options ${FORGE_PACKAGE_CONSUMER_BUILD_OPTIONS}
      --test-command forge_package_plugins_log_otlp
)
set_tests_properties(test_forge_package_plugins_log_otlp PROPERTIES DEPENDS test_forge_package_install)
if(FORGE_HAS_ROCKSDB)
   add_test(
      NAME test_forge_package_rocksdb_component
      COMMAND ${CMAKE_CTEST_COMMAND}
         --build-and-test
         ${CMAKE_CURRENT_SOURCE_DIR}/package_rocksdb_component
         ${CMAKE_BINARY_DIR}/forge-package-rocksdb-component
         --build-generator ${CMAKE_GENERATOR}
         --build-options ${FORGE_PACKAGE_CONSUMER_BUILD_OPTIONS}
         --test-command forge_package_rocksdb_component
   )
   set_tests_properties(test_forge_package_rocksdb_component PROPERTIES DEPENDS test_forge_package_install)

   add_test(
      NAME test_forge_package_plugins_db_rocksdb
      COMMAND ${CMAKE_CTEST_COMMAND}
         --build-and-test
         ${CMAKE_CURRENT_SOURCE_DIR}/package_plugins_db_rocksdb
         ${CMAKE_BINARY_DIR}/forge-package-plugins-db-rocksdb
         --build-generator ${CMAKE_GENERATOR}
         --build-options ${FORGE_PACKAGE_CONSUMER_BUILD_OPTIONS}
         --test-command forge_package_plugins_db_rocksdb
   )
   set_tests_properties(test_forge_package_plugins_db_rocksdb PROPERTIES DEPENDS test_forge_package_install)
else()
   add_test(
      NAME test_forge_package_rocksdb_component
      COMMAND ${CMAKE_COMMAND} -E echo "Skipping test_forge_package_rocksdb_component: RocksDB is not available"
   )
   add_test(
      NAME test_forge_package_plugins_db_rocksdb
      COMMAND ${CMAKE_COMMAND} -E echo "Skipping test_forge_package_plugins_db_rocksdb: RocksDB is not available"
   )
endif()
add_test(
   NAME test_forge_package_plugins_component
   COMMAND ${CMAKE_CTEST_COMMAND}
      --build-and-test
      ${CMAKE_CURRENT_SOURCE_DIR}/package_plugins_component
      ${CMAKE_BINARY_DIR}/forge-package-plugins-component
      --build-generator ${CMAKE_GENERATOR}
      --build-options ${FORGE_PACKAGE_CONSUMER_BUILD_OPTIONS}
      --test-command forge_package_plugins_component
)
set_tests_properties(test_forge_package_plugins_component PROPERTIES DEPENDS test_forge_package_install)
add_test(
   NAME test_forge_package_all_missing_dependency
   COMMAND ${CMAKE_COMMAND}
      -DFORGE_PACKAGE_TEST_SOURCE_DIR=${CMAKE_CURRENT_SOURCE_DIR}/package_all_missing_dependency
      -DFORGE_PACKAGE_TEST_BINARY_DIR=${CMAKE_BINARY_DIR}/forge-package-all-missing-dependency
      -DFORGE_PACKAGE_TEST_PREFIX=${FORGE_PACKAGE_INSTALL_PREFIX}
      -DFORGE_PACKAGE_TEST_GENERATOR=${CMAKE_GENERATOR}
      -DFORGE_PACKAGE_TEST_CXX_COMPILER=${CMAKE_CXX_COMPILER}
      -DFORGE_PACKAGE_TEST_C_COMPILER=${CMAKE_C_COMPILER}
      -DFORGE_PACKAGE_TEST_OSX_SYSROOT=${CMAKE_OSX_SYSROOT}
      -P ${CMAKE_CURRENT_SOURCE_DIR}/package_all_missing_dependency/expect_configure_failure.cmake
)
set_tests_properties(test_forge_package_all_missing_dependency PROPERTIES DEPENDS test_forge_package_install)
