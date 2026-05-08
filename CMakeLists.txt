cmake_minimum_required(VERSION 3.31)

project(storlane-fc VERSION 0.1.0 LANGUAGES C CXX)

include(GNUInstallDirs)

if(CMAKE_CURRENT_SOURCE_DIR STREQUAL CMAKE_SOURCE_DIR)
   include(CTest)
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

option(FC_ENABLE_VENDOR_TESTS "Build donor dependency tests and benchmarks" OFF)

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
   COMPONENTS headers date_time chrono iostreams thread
)

find_package(OpenSSL 3 REQUIRED COMPONENTS Crypto)

function(fc_add_header_only_boost_target component)
   if(NOT TARGET Boost::${component})
      add_library(Boost::${component} INTERFACE IMPORTED)
      set_property(
         TARGET Boost::${component}
         APPEND PROPERTY INTERFACE_LINK_LIBRARIES Boost::headers
      )
   endif()
endfunction()

fc_add_header_only_boost_target(asio)
fc_add_header_only_boost_target(beast)
fc_add_header_only_boost_target(crc)
fc_add_header_only_boost_target(dll)
fc_add_header_only_boost_target(interprocess)
fc_add_header_only_boost_target(multi_index)
fc_add_header_only_boost_target(multiprecision)

if(NOT TARGET Boost::included_unit_test_framework)
   add_library(Boost::included_unit_test_framework INTERFACE IMPORTED)
   set_property(
      TARGET Boost::included_unit_test_framework
      APPEND PROPERTY INTERFACE_LINK_LIBRARIES Boost::headers
   )
endif()

find_package(Git QUIET)
set(FC_GIT_REVISION_SHA "unknown")
set(FC_GIT_REVISION_UNIX_TIMESTAMP 0)

if(Git_FOUND AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/.git")
   set(_fc_git_rev_parse_result 1)
   execute_process(
      COMMAND "${GIT_EXECUTABLE}" rev-parse HEAD
      WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
      OUTPUT_VARIABLE FC_GIT_REVISION_SHA
      RESULT_VARIABLE _fc_git_rev_parse_result
      OUTPUT_STRIP_TRAILING_WHITESPACE
      ERROR_QUIET
   )
   if(NOT _fc_git_rev_parse_result EQUAL 0)
      set(FC_GIT_REVISION_SHA "unknown")
   endif()

   set(_fc_git_log_result 1)
   execute_process(
      COMMAND "${GIT_EXECUTABLE}" log -1 --format=%ct
      WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
      OUTPUT_VARIABLE FC_GIT_REVISION_UNIX_TIMESTAMP
      RESULT_VARIABLE _fc_git_log_result
      OUTPUT_STRIP_TRAILING_WHITESPACE
      ERROR_QUIET
   )
   if(NOT _fc_git_log_result EQUAL 0)
      set(FC_GIT_REVISION_UNIX_TIMESTAMP 0)
   endif()
endif()

file(MAKE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/src")
configure_file(
   "${CMAKE_CURRENT_SOURCE_DIR}/src/git_revision.cpp.in"
   "${CMAKE_CURRENT_BINARY_DIR}/src/git_revision.cpp"
   @ONLY
)

add_subdirectory(secp256k1)
add_subdirectory(libraries/bn256/src)
# Keep donor submodules clean and avoid building their standalone tests/benchmarks
# as part of the default fc build.
add_subdirectory(libraries/bls12-381 EXCLUDE_FROM_ALL)

if(NOT FC_ENABLE_VENDOR_TESTS)
   file(
      WRITE "${CMAKE_CURRENT_BINARY_DIR}/CTestCustom.cmake"
      "set(CTEST_CUSTOM_TESTS_IGNORE bls12-381 bls12-381-nobmi2)\n"
   )
endif()

set(
   fc_sources
   src/uint128.cpp
   src/variant.cpp
   src/exception.cpp
   src/variant_object.cpp
   src/string.cpp
   src/time.cpp
   src/mock_time.cpp
   src/utf8.cpp
   src/io/datastream.cpp
   src/io/json.cpp
   src/io/varint.cpp
   src/io/fstream.cpp
   src/io/console.cpp
   src/filesystem.cpp
   src/interprocess/file_mapping.cpp
   src/log/log_message.cpp
   src/log/logger.cpp
   src/log/appender.cpp
   src/log/console_appender.cpp
   src/log/dmlog_appender.cpp
   src/log/logger_config.cpp
   src/crypto/_digest_common.cpp
   src/crypto/aes.cpp
   src/crypto/crc.cpp
   src/crypto/city.cpp
   src/crypto/base58.cpp
   src/crypto/bigint.cpp
   src/crypto/hex.cpp
   src/crypto/sha1.cpp
   src/crypto/sha3.cpp
   src/crypto/ripemd160.cpp
   src/crypto/sha256.cpp
   src/crypto/sha224.cpp
   src/crypto/sha512.cpp
   src/crypto/elliptic_common.cpp
   src/crypto/elliptic_impl_priv.cpp
   src/crypto/elliptic_secp256k1.cpp
   src/crypto/elliptic_r1.cpp
   src/crypto/elliptic_webauthn.cpp
   src/crypto/rand.cpp
   src/crypto/public_key.cpp
   src/crypto/private_key.cpp
   src/crypto/signature.cpp
   src/crypto/bls_private_key.cpp
   src/crypto/bls_public_key.cpp
   src/crypto/bls_signature.cpp
   src/crypto/bls_utils.cpp
   src/crypto/modular_arithmetic.cpp
   src/crypto/blake2.cpp
   src/crypto/k1_recover.cpp
   src/network/url.cpp
   src/network/http/http_client.cpp
   src/compress/zlib.cpp
   src/log/gelf_appender.cpp
   "${CMAKE_CURRENT_BINARY_DIR}/src/git_revision.cpp"
)

file(GLOB_RECURSE fc_headers "${CMAKE_CURRENT_SOURCE_DIR}" *.hpp *.h)

add_library(fc ${fc_sources} ${fc_headers})

target_compile_definitions(
   fc
   PUBLIC OPENSSL_API_COMPAT=0x30000000L
          OPENSSL_NO_DEPRECATED
)

find_path(GMP_INCLUDE_DIR NAMES gmp.h)
find_library(GMP_LIBRARY gmp)
if(NOT GMP_LIBRARY MATCHES ${CMAKE_SHARED_LIBRARY_SUFFIX})
   message(FATAL_ERROR "GMP shared library not found")
endif()
set(gmp_library_type SHARED)
message(STATUS "GMP: ${GMP_LIBRARY}, ${GMP_INCLUDE_DIR}")
add_library(GMP::gmp ${gmp_library_type} IMPORTED)
set_target_properties(
   GMP::gmp PROPERTIES
   IMPORTED_LOCATION ${GMP_LIBRARY}
   INTERFACE_INCLUDE_DIRECTORIES ${GMP_INCLUDE_DIR}
)

target_link_libraries(fc PUBLIC GMP::gmp)

# fc picks up a dependency on zlib via Boost::iostreams, however in some versions of
# cmake/boost the Boost::iostreams target does not add zlib itself, so keep it explicit.
find_package(ZLIB REQUIRED)

target_include_directories(fc PUBLIC include)

set_source_files_properties(
   src/crypto/elliptic_webauthn.cpp
   PROPERTIES INCLUDE_DIRECTORIES
              "${CMAKE_CURRENT_SOURCE_DIR}/include/fc/crypto/webauthn_json/include"
)

if(WIN32)
   target_link_libraries(fc PUBLIC ws2_32 mswsock userenv)
endif()

if(APPLE)
   find_library(security_framework Security)
   find_library(corefoundation_framework CoreFoundation)
endif()

target_link_libraries(
   fc PUBLIC
      Boost::date_time
      Boost::chrono
      Boost::iostreams
      Boost::interprocess
      Boost::multi_index
      Boost::dll
      Boost::multiprecision
      Boost::beast
      Boost::asio
      Boost::thread
      Boost::crc
      Threads::Threads
      OpenSSL::Crypto
      ZLIB::ZLIB
      ${PLATFORM_SPECIFIC_LIBS}
      ${CMAKE_DL_LIBS}
      secp256k1
      bls12-381
      ${security_framework}
      ${corefoundation_framework}
)

if(DEFINED BUILD_TESTING AND BUILD_TESTING)
   add_subdirectory(test)
endif()

install(
   TARGETS fc
   LIBRARY DESTINATION ${CMAKE_INSTALL_FULL_LIBDIR} COMPONENT dev EXCLUDE_FROM_ALL
   ARCHIVE DESTINATION ${CMAKE_INSTALL_FULL_LIBDIR} COMPONENT dev EXCLUDE_FROM_ALL
)

install(
   DIRECTORY include/fc
   DESTINATION ${CMAKE_INSTALL_FULL_INCLUDEDIR} COMPONENT dev EXCLUDE_FROM_ALL
)

install(
   TARGETS bls12-381 ARCHIVE
   DESTINATION "${CMAKE_INSTALL_FULL_LIBDIR}" COMPONENT dev EXCLUDE_FROM_ALL
   PUBLIC_HEADER DESTINATION "${CMAKE_INSTALL_FULL_INCLUDEDIR}/bls12-381" COMPONENT dev EXCLUDE_FROM_ALL
)
