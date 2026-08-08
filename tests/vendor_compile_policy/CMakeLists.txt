cmake_minimum_required(VERSION 3.31)

project(forge_vendor_compile_policy_test LANGUAGES C CXX)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
get_property(_forge_generator_is_multi_config GLOBAL PROPERTY GENERATOR_IS_MULTI_CONFIG)
if(_forge_generator_is_multi_config)
   file(WRITE "${CMAKE_BINARY_DIR}/generator-is-multi-config.txt" "true\n")
else()
   file(WRITE "${CMAKE_BINARY_DIR}/generator-is-multi-config.txt" "false\n")
endif()
file(
   WRITE "${CMAKE_BINARY_DIR}/compiler-frontends.txt"
   "C=${CMAKE_C_COMPILER_FRONTEND_VARIANT}\nCXX=${CMAKE_CXX_COMPILER_FRONTEND_VARIANT}\n"
)

foreach(_forge_test_language C CXX)
   if(CMAKE_${_forge_test_language}_COMPILER_FRONTEND_VARIANT STREQUAL "GNU")
      set(CMAKE_${_forge_test_language}_FLAGS_DEBUG "${CMAKE_${_forge_test_language}_FLAGS_DEBUG} -O0")
      add_compile_options(
         "$<$<AND:$<CONFIG:Debug>,$<COMPILE_LANGUAGE:${_forge_test_language}>>:-fsanitize=address,undefined>"
      )
   elseif(CMAKE_${_forge_test_language}_COMPILER_FRONTEND_VARIANT STREQUAL "MSVC")
      set(CMAKE_${_forge_test_language}_FLAGS_DEBUG "${CMAKE_${_forge_test_language}_FLAGS_DEBUG} /Od")
      add_compile_options(
         "$<$<AND:$<CONFIG:Debug>,$<COMPILE_LANGUAGE:${_forge_test_language}>>:/fsanitize=address>"
      )
   else()
      message(
         FATAL_ERROR
         "Unsupported ${_forge_test_language} compiler frontend in vendor policy fixture: ${CMAKE_${_forge_test_language}_COMPILER_FRONTEND_VARIANT}"
      )
   endif()
endforeach()

include("${FORGE_SOURCE_DIR}/cmake/ForgeVendorCompilePolicy.cmake")

_forge_vendor_debug_optimization_flag(C GNU _forge_test_gnu_optimization)
_forge_vendor_debug_optimization_flag(CXX MSVC _forge_test_msvc_optimization)
if(NOT _forge_test_gnu_optimization STREQUAL "-O2" OR NOT _forge_test_msvc_optimization STREQUAL "/O2")
   message(FATAL_ERROR "Vendored Debug optimization frontend mapping is invalid")
endif()

add_library(vendor_c STATIC vendor_c.c)
add_library(vendor_cxx STATIC vendor_cxx.cpp)
add_library(vendor_alignment STATIC vendor_alignment.c vendor_alignment.cpp)
add_library(forge_owned STATIC forge_owned.cpp)

forge_apply_vendored_implementation_policy(vendor_c)
forge_apply_vendored_implementation_policy(vendor_cxx)
forge_apply_vendored_implementation_policy(vendor_alignment DISABLED_SANITIZERS alignment)
