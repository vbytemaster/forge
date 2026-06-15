cmake_minimum_required(VERSION 3.31)

project(storlane-fc VERSION 3.0.0 LANGUAGES C CXX ASM)

include(GNUInstallDirs)
include(CheckCXXSourceCompiles)
include(CMakePackageConfigHelpers)
list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")

if(CMAKE_CURRENT_SOURCE_DIR STREQUAL CMAKE_SOURCE_DIR)
   include(CTest)
endif()

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

option(FCL_ENABLE_MODULES "Build FCL public module interfaces when supported" ON)
option(FCL_ENABLE_VENDOR_TESTS "Build donor dependency tests and benchmarks" OFF)
option(FCL_ENABLE_TUI "Build FCL Notcurses-backed terminal UI library when notcurses-core is available" ON)
option(FCL_ENABLE_LIBP2P_INTEROP "Run live go-libp2p/rust-libp2p interoperability tests" OFF)
option(FCL_ENABLE_ROCKSDB "Build RocksDB-backed optional FCL features when RocksDB is available" ON)

set(FCL_LIBP2P_DONORS_ROOT "" CACHE PATH "Path to workspace donors directory containing go-libp2p, rust-libp2p and libp2p-specs")
if(NOT FCL_LIBP2P_DONORS_ROOT)
   set(_fcl_default_donors_root "/Users/vladimirtarnakin/.openclaw/workspace/Projects/Storlane/donors")
   if(EXISTS "${_fcl_default_donors_root}")
      set(FCL_LIBP2P_DONORS_ROOT "${_fcl_default_donors_root}" CACHE PATH "Path to workspace donors directory containing go-libp2p, rust-libp2p and libp2p-specs" FORCE)
   endif()
endif()

if(FCL_ENABLE_MODULES AND CMAKE_CXX_COMPILER_ID STREQUAL "AppleClang")
   message(FATAL_ERROR "FCL_ENABLE_MODULES=ON requires module-capable Homebrew LLVM clang++; AppleClang is not a supported FCL module toolchain")
endif()

if("${CMAKE_GENERATOR}" STREQUAL "Ninja")
   add_compile_options(-fdiagnostics-color=always)
endif()

if(APPLE)
   message(STATUS "Configuring storlane-fc on macOS")
   set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wall")
   set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall")
else()
   message(STATUS "Configuring storlane-fc on Linux")
   set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wall")
   set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall")
   set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -latomic")
endif()

add_compile_definitions(BOOST_TEST_DEFAULTS_TO_CORE_DUMP)
add_compile_definitions(BOOST_UNORDERED_DISABLE_NEON)

set(CMAKE_THREAD_PREFER_PTHREAD TRUE)
set(THREADS_PREFER_PTHREAD_FLAG TRUE)
find_package(Threads REQUIRED)

find_package(
   Boost 1.90 REQUIRED CONFIG
   COMPONENTS headers date_time iostreams url program_options
)
find_package(boost_stacktrace_basic CONFIG QUIET)
check_cxx_source_compiles(
   "#include <stacktrace>
   #ifndef __cpp_lib_stacktrace
   #error no std stacktrace feature macro
   #endif
   int main() { auto trace = std::stacktrace::current(); return static_cast<int>(trace.size()); }"
   FCL_HAS_STD_STACKTRACE
)
if(FCL_HAS_STD_STACKTRACE)
   message(STATUS "FCL log stacktrace backend: std::stacktrace")
elseif(TARGET Boost::stacktrace_basic)
   message(STATUS "FCL log stacktrace backend: Boost::stacktrace_basic")
else()
   message(STATUS "FCL log stacktrace backend: unavailable")
endif()

find_package(OpenSSL 3 REQUIRED COMPONENTS SSL Crypto)
find_package(Ngtcp2 REQUIRED)
set(FCL_HAS_ROCKSDB OFF)
if(FCL_ENABLE_ROCKSDB)
   find_package(RocksDB QUIET)
   if(TARGET RocksDB::rocksdb)
      set(FCL_HAS_ROCKSDB ON)
      message(STATUS "FCL RocksDB backend: RocksDB::rocksdb")
   else()
      message(STATUS "Skipping FCL RocksDB-backed features: RocksDB not found")
   endif()
else()
   message(STATUS "FCL RocksDB-backed features: disabled")
endif()
find_package(glaze 7.5 CONFIG REQUIRED)
if(TARGET glaze::glaze)
   message(STATUS "FCL codec backend: glaze::glaze")
else()
   message(FATAL_ERROR "Glaze was found, but target glaze::glaze is not available")
endif()

set(FCL_TUI_NOTCURSES_TARGET "")
if(FCL_ENABLE_TUI)
   find_package(Notcurses CONFIG QUIET)
   if(TARGET Notcurses::notcurses-core)
      set(FCL_TUI_NOTCURSES_TARGET Notcurses::notcurses-core)
   elseif(TARGET notcurses::notcurses-core)
      set(FCL_TUI_NOTCURSES_TARGET notcurses::notcurses-core)
   elseif(TARGET notcurses-core)
      set(FCL_TUI_NOTCURSES_TARGET notcurses-core)
   endif()

   if(NOT FCL_TUI_NOTCURSES_TARGET)
      find_package(PkgConfig QUIET)
      if(PkgConfig_FOUND)
         pkg_check_modules(NOTCURSES_CORE QUIET IMPORTED_TARGET notcurses-core)
         if(TARGET PkgConfig::NOTCURSES_CORE)
            set(FCL_TUI_NOTCURSES_TARGET PkgConfig::NOTCURSES_CORE)
         endif()
      endif()
   endif()

   if(FCL_TUI_NOTCURSES_TARGET)
      message(STATUS "FCL TUI backend: ${FCL_TUI_NOTCURSES_TARGET}")
   else()
      message(STATUS "Skipping fcl_tui: notcurses-core not found; install notcurses-core or set FCL_ENABLE_TUI=OFF")
   endif()
endif()

function(fcl_add_header_only_boost_target component)
   if(NOT TARGET Boost::${component})
      add_library(Boost::${component} INTERFACE IMPORTED)
      set_property(
         TARGET Boost::${component}
         APPEND PROPERTY INTERFACE_LINK_LIBRARIES Boost::headers
      )
   endif()
endfunction()

fcl_add_header_only_boost_target(asio)
fcl_add_header_only_boost_target(beast)
fcl_add_header_only_boost_target(crc)
fcl_add_header_only_boost_target(dll)
fcl_add_header_only_boost_target(multi_index)
fcl_add_header_only_boost_target(multiprecision)

if(NOT TARGET Boost::included_unit_test_framework)
   add_library(Boost::included_unit_test_framework INTERFACE IMPORTED)
   set_property(
      TARGET Boost::included_unit_test_framework
      APPEND PROPERTY INTERFACE_LINK_LIBRARIES Boost::headers
   )
endif()

find_package(Git QUIET)
set(FCL_GIT_REVISION_SHA "unknown")
set(FCL_GIT_REVISION_UNIX_TIMESTAMP 0)

if(Git_FOUND AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/.git")
   execute_process(
      COMMAND "${GIT_EXECUTABLE}" rev-parse HEAD
      WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
      OUTPUT_VARIABLE FCL_GIT_REVISION_SHA
      RESULT_VARIABLE _fcl_git_rev_parse_result
      OUTPUT_STRIP_TRAILING_WHITESPACE
      ERROR_QUIET
   )
   if(NOT _fcl_git_rev_parse_result EQUAL 0)
      set(FCL_GIT_REVISION_SHA "unknown")
   endif()

   execute_process(
      COMMAND "${GIT_EXECUTABLE}" log -1 --format=%ct
      WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
      OUTPUT_VARIABLE FCL_GIT_REVISION_UNIX_TIMESTAMP
      RESULT_VARIABLE _fcl_git_log_result
      OUTPUT_STRIP_TRAILING_WHITESPACE
      ERROR_QUIET
   )
   if(NOT _fcl_git_log_result EQUAL 0)
      set(FCL_GIT_REVISION_UNIX_TIMESTAMP 0)
   endif()
endif()

find_path(GMP_INCLUDE_DIR NAMES gmp.h)
find_library(GMP_LIBRARY gmp)
if(NOT GMP_LIBRARY MATCHES ${CMAKE_SHARED_LIBRARY_SUFFIX})
   message(FATAL_ERROR "GMP shared library not found")
endif()
message(STATUS "GMP: ${GMP_LIBRARY}, ${GMP_INCLUDE_DIR}")
add_library(GMP::gmp SHARED IMPORTED)
set_target_properties(
   GMP::gmp PROPERTIES
   IMPORTED_LOCATION ${GMP_LIBRARY}
   INTERFACE_INCLUDE_DIRECTORIES ${GMP_INCLUDE_DIR}
)

set(
   FCL_SOURCE_INCLUDE_DIRS
   "${CMAKE_CURRENT_SOURCE_DIR}/libraries/core/include"
   "${CMAKE_CURRENT_SOURCE_DIR}/libraries/exceptions/include"
   "${CMAKE_CURRENT_SOURCE_DIR}/libraries/reflect/include"
   "${CMAKE_CURRENT_SOURCE_DIR}/libraries/schema/include"
   "${CMAKE_CURRENT_SOURCE_DIR}/libraries/variant/include"
   "${CMAKE_CURRENT_SOURCE_DIR}/libraries/raw/include"
   "${CMAKE_CURRENT_SOURCE_DIR}/libraries/json/include"
   "${CMAKE_CURRENT_SOURCE_DIR}/libraries/config/include"
   "${CMAKE_CURRENT_SOURCE_DIR}/libraries/yaml/include"
   "${CMAKE_CURRENT_SOURCE_DIR}/libraries/program_options/include"
   "${CMAKE_CURRENT_SOURCE_DIR}/libraries/env/include"
   "${CMAKE_CURRENT_SOURCE_DIR}/libraries/api/include"
   "${CMAKE_CURRENT_SOURCE_DIR}/libraries/api_transport/include"
   "${CMAKE_CURRENT_SOURCE_DIR}/libraries/log/include"
   "${CMAKE_CURRENT_SOURCE_DIR}/libraries/otlp/include"
   "${CMAKE_CURRENT_SOURCE_DIR}/libraries/crypto/include"
   "${CMAKE_CURRENT_SOURCE_DIR}/libraries/multiformats/include"
   "${CMAKE_CURRENT_SOURCE_DIR}/libraries/asio/include"
   "${CMAKE_CURRENT_SOURCE_DIR}/libraries/http/include"
   "${CMAKE_CURRENT_SOURCE_DIR}/libraries/websocket/include"
   "${CMAKE_CURRENT_SOURCE_DIR}/libraries/transport/include"
   "${CMAKE_CURRENT_SOURCE_DIR}/libraries/tcp/include"
   "${CMAKE_CURRENT_SOURCE_DIR}/libraries/yamux/include"
   "${CMAKE_CURRENT_SOURCE_DIR}/libraries/quic/include"
   "${CMAKE_CURRENT_SOURCE_DIR}/libraries/stcp/include"
   "${CMAKE_CURRENT_SOURCE_DIR}/libraries/p2p/include"
   "${CMAKE_CURRENT_SOURCE_DIR}/libraries/app/include"
   "${CMAKE_CURRENT_SOURCE_DIR}/plugins/signature_provider/include"
   "${CMAKE_CURRENT_SOURCE_DIR}/plugins/p2p_node/include"
   "${CMAKE_CURRENT_SOURCE_DIR}/plugins/p2p_api_resolver/include"
   "${CMAKE_CURRENT_SOURCE_DIR}/plugins/p2p_diagnostics/include"
   "${CMAKE_CURRENT_SOURCE_DIR}/plugins/p2p_pubsub/include"
)

if(FCL_ENABLE_TUI AND FCL_TUI_NOTCURSES_TARGET)
   list(APPEND FCL_SOURCE_INCLUDE_DIRS "${CMAKE_CURRENT_SOURCE_DIR}/libraries/tui/include")
endif()

set(FCL_PUBLIC_INCLUDE_DIRS)
foreach(_fcl_include_dir IN LISTS FCL_SOURCE_INCLUDE_DIRS)
   list(APPEND FCL_PUBLIC_INCLUDE_DIRS "$<BUILD_INTERFACE:${_fcl_include_dir}>")
endforeach()
list(APPEND FCL_PUBLIC_INCLUDE_DIRS "$<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>")

function(fcl_target_modules_at target module_path)
   file(
      GLOB _fcl_target_module_files CONFIGURE_DEPENDS
      "${CMAKE_CURRENT_SOURCE_DIR}/include/fcl/${module_path}/*.cppm"
   )
   if(NOT _fcl_target_module_files)
      message(FATAL_ERROR "No public FCL module interfaces found for ${target} in ${CMAKE_CURRENT_SOURCE_DIR}/include/fcl/${module_path}")
   endif()

   string(REPLACE "/" "_" _fcl_module_var "${module_path}")
   foreach(_fcl_disabled_module IN LISTS FCL_${_fcl_module_var}_DISABLED_MODULES)
      list(FILTER _fcl_target_module_files EXCLUDE REGEX "/${_fcl_disabled_module}\\.cppm$")
   endforeach()

   target_sources(
      ${target}
      PUBLIC
         FILE_SET CXX_MODULES
         BASE_DIRS "${CMAKE_CURRENT_SOURCE_DIR}/include"
         FILES ${_fcl_target_module_files}
   )
   target_compile_features(${target} PUBLIC cxx_std_23)
endfunction()

function(fcl_target_modules target lib)
   fcl_target_modules_at(${target} ${lib})
endfunction()


add_library(secp256k1-internal INTERFACE)
target_include_directories(
   secp256k1-internal INTERFACE
      "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/vendor/secp256k1/src>"
)
target_compile_definitions(
   secp256k1-internal INTERFACE
      ENABLE_MODULE_RECOVERY=1
      COMB_BLOCKS=43
      COMB_TEETH=6
      ECMULT_WINDOW_SIZE=15
      SECP256K1_STATIC=1
)
if(CMAKE_SYSTEM_PROCESSOR STREQUAL x86_64)
   target_compile_definitions(secp256k1-internal INTERFACE USE_ASM_X86_64=1)
endif()
target_compile_options(secp256k1-internal INTERFACE -Wno-unused-function)

add_library(
   secp256k1 STATIC
   vendor/secp256k1/src/secp256k1.c
   vendor/secp256k1/src/precomputed_ecmult.c
   vendor/secp256k1/src/precomputed_ecmult_gen.c
)
target_include_directories(
   secp256k1 PUBLIC
      "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/vendor/secp256k1>"
      "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/vendor/secp256k1/include>"
      "$<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>"
)
target_link_libraries(secp256k1 PRIVATE secp256k1-internal)

if(FCL_ENABLE_VENDOR_TESTS)
   add_executable(secp256k1-bench vendor/secp256k1/src/bench.c)
   target_link_libraries(secp256k1-bench secp256k1 secp256k1-internal)

   add_executable(secp256k1-tests vendor/secp256k1/src/tests.c)
   target_link_libraries(secp256k1-tests secp256k1 secp256k1-internal)
   add_test(NAME secp256k1-tests COMMAND secp256k1-tests)

   add_executable(secp256k1-exhaustive-tests vendor/secp256k1/src/tests_exhaustive.c)
   target_link_libraries(secp256k1-exhaustive-tests secp256k1 secp256k1-internal)
   add_test(NAME secp256k1-exhaustive-tests COMMAND secp256k1-exhaustive-tests)
endif()

add_subdirectory(vendor/bn256/src)
add_subdirectory(vendor/bls12-381 EXCLUDE_FROM_ALL)
set_property(
   TARGET bls12-381 PROPERTY INTERFACE_INCLUDE_DIRECTORIES
      "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/vendor/bls12-381/include>"
      "$<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>"
)

if(NOT FCL_ENABLE_VENDOR_TESTS)
   file(
      WRITE "${CMAKE_CURRENT_BINARY_DIR}/CTestCustom.cmake"
      "set(CTEST_CUSTOM_TESTS_IGNORE bls12-381 bls12-381-nobmi2)\n"
   )
endif()


add_subdirectory(libraries/core)
add_subdirectory(libraries/exceptions)
add_subdirectory(libraries/reflect)
add_subdirectory(libraries/schema)
add_subdirectory(libraries/variant)
add_subdirectory(libraries/raw)
add_subdirectory(libraries/config)
add_subdirectory(libraries/json)
add_subdirectory(libraries/yaml)
add_subdirectory(libraries/program_options)
add_subdirectory(libraries/env)
add_subdirectory(libraries/api)
add_subdirectory(libraries/api_transport)
add_subdirectory(libraries/log)
add_subdirectory(libraries/crypto)
add_subdirectory(libraries/multiformats)
add_subdirectory(libraries/asio)
add_subdirectory(libraries/http)
add_subdirectory(libraries/otlp)
add_subdirectory(libraries/websocket)
add_subdirectory(libraries/transport)
add_subdirectory(libraries/tcp)
add_subdirectory(libraries/stcp)
add_subdirectory(libraries/yamux)
add_subdirectory(libraries/quic)
add_subdirectory(libraries/p2p)
add_subdirectory(libraries/app)
add_subdirectory(plugins)
if(FCL_ENABLE_TUI AND FCL_TUI_NOTCURSES_TARGET)
   add_subdirectory(libraries/tui)
endif()

set(
   FCL_LEAF_TARGETS
   fcl_core
   fcl_exceptions
   fcl_reflect
   fcl_schema
   fcl_variant
   fcl_raw
   fcl_config
   fcl_json
   fcl_yaml
   fcl_program_options
   fcl_env
   fcl_api
   fcl_api_transport
   fcl_log
   fcl_otlp
   fcl_crypto
   fcl_multiformats
   fcl_asio
   fcl_http
   fcl_websocket
   fcl_transport
   fcl_tcp
   fcl_stcp
   fcl_yamux
   fcl_quic
   fcl_p2p
   fcl_app
   fcl_plugin_signature_provider
   fcl_plugin_http_server
   fcl_plugin_p2p_node
   fcl_plugin_p2p_api_resolver
   fcl_plugin_p2p_diagnostics
   fcl_plugin_p2p_pubsub
   fcl_plugins
)

if(TARGET fcl_tui)
   list(APPEND FCL_LEAF_TARGETS fcl_tui)
endif()

add_library(fcl STATIC fcl.cpp)

target_include_directories(fcl PUBLIC ${FCL_PUBLIC_INCLUDE_DIRS})

target_link_libraries(
   fcl PUBLIC
      ${FCL_LEAF_TARGETS}
)

if(DEFINED BUILD_TESTING AND BUILD_TESTING)
   add_subdirectory(tests)
endif()

install(
   TARGETS fcl ${FCL_LEAF_TARGETS}
   EXPORT FCLTargets
   LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR} COMPONENT dev EXCLUDE_FROM_ALL
   ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR} COMPONENT dev EXCLUDE_FROM_ALL
   RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR} COMPONENT dev EXCLUDE_FROM_ALL
   FILE_SET CXX_MODULES DESTINATION ${CMAKE_INSTALL_INCLUDEDIR} COMPONENT dev EXCLUDE_FROM_ALL
)

install(
   TARGETS secp256k1-internal secp256k1 bls12-381
   EXPORT FCLTargets
   LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR} COMPONENT dev EXCLUDE_FROM_ALL
   ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR} COMPONENT dev EXCLUDE_FROM_ALL
   PUBLIC_HEADER DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/bls12-381 COMPONENT dev EXCLUDE_FROM_ALL
)

install(
   DIRECTORY libraries/core/include/fcl
             libraries/exceptions/include/fcl
             libraries/reflect/include/fcl
             libraries/schema/include/fcl
             libraries/variant/include/fcl
             libraries/raw/include/fcl
             libraries/json/include/fcl
             libraries/config/include/fcl
             libraries/yaml/include/fcl
             libraries/program_options/include/fcl
             libraries/env/include/fcl
             libraries/api/include/fcl
             libraries/api_transport/include/fcl
             libraries/log/include/fcl
             libraries/otlp/include/fcl
             libraries/crypto/include/fcl
             libraries/multiformats/include/fcl
             libraries/asio/include/fcl
             libraries/http/include/fcl
             libraries/websocket/include/fcl
             libraries/transport/include/fcl
             libraries/tcp/include/fcl
             libraries/yamux/include/fcl
             libraries/quic/include/fcl
             libraries/stcp/include/fcl
             libraries/p2p/include/fcl
             libraries/app/include/fcl
             plugins/signature_provider/include/fcl
             plugins/http_server/include/fcl
             plugins/p2p_node/include/fcl
             plugins/p2p_api_resolver/include/fcl
             plugins/p2p_diagnostics/include/fcl
             plugins/p2p_pubsub/include/fcl
   DESTINATION ${CMAKE_INSTALL_INCLUDEDIR} COMPONENT dev EXCLUDE_FROM_ALL
)

if(TARGET fcl_tui)
   install(
      DIRECTORY libraries/tui/include/fcl
      DESTINATION ${CMAKE_INSTALL_INCLUDEDIR} COMPONENT dev EXCLUDE_FROM_ALL
   )
endif()

install(
   DIRECTORY vendor/secp256k1/include/
   DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
   COMPONENT dev
   EXCLUDE_FROM_ALL
)
install(
   DIRECTORY vendor/bls12-381/include/
   DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
   COMPONENT dev
   EXCLUDE_FROM_ALL
)

install(
   FILES
      "${CMAKE_CURRENT_SOURCE_DIR}/LICENSE"
      "${CMAKE_CURRENT_SOURCE_DIR}/NOTICE"
      "${CMAKE_CURRENT_SOURCE_DIR}/AUTHORS"
      "${CMAKE_CURRENT_SOURCE_DIR}/PROVENANCE.md"
      "${CMAKE_CURRENT_SOURCE_DIR}/THIRD_PARTY_LICENSES"
   DESTINATION "${CMAKE_INSTALL_DATADIR}/doc/FCL"
   COMPONENT dev
   EXCLUDE_FROM_ALL
)

install(
   EXPORT FCLTargets
   FILE FCLTargets.cmake
   NAMESPACE FCL::
   DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/FCL
   COMPONENT dev
   EXCLUDE_FROM_ALL
)

configure_package_config_file(
   "${CMAKE_CURRENT_SOURCE_DIR}/cmake/FCLConfig.cmake.in"
   "${CMAKE_CURRENT_BINARY_DIR}/FCLConfig.cmake"
   INSTALL_DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/FCL"
)
write_basic_package_version_file(
   "${CMAKE_CURRENT_BINARY_DIR}/FCLConfigVersion.cmake"
   VERSION ${PROJECT_VERSION}
   COMPATIBILITY SameMajorVersion
)
install(
   FILES
      "${CMAKE_CURRENT_BINARY_DIR}/FCLConfig.cmake"
      "${CMAKE_CURRENT_BINARY_DIR}/FCLConfigVersion.cmake"
   DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/FCL
   COMPONENT dev
   EXCLUDE_FROM_ALL
)
install(
   DIRECTORY cmake/
   DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/FCL/modules
   COMPONENT dev
   EXCLUDE_FROM_ALL
   FILES_MATCHING
      PATTERN "*.cmake"
      PATTERN "FCLConfig.cmake.in" EXCLUDE
)
