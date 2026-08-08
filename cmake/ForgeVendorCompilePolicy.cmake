include_guard(GLOBAL)

include(CheckCompilerFlag)

function(_forge_vendor_debug_optimization_flag language frontend output)
   if(frontend STREQUAL "GNU")
      set(_forge_vendor_optimization_flag "-O2")
   elseif(frontend STREQUAL "MSVC")
      set(_forge_vendor_optimization_flag "/O2")
   else()
      message(FATAL_ERROR "Unsupported ${language} compiler frontend for vendored Debug optimization: ${frontend}")
   endif()
   set(${output} "${_forge_vendor_optimization_flag}" PARENT_SCOPE)
endfunction()

function(forge_apply_vendored_implementation_policy target)
   cmake_parse_arguments(PARSE_ARGV 1 ARG "" "" "DISABLED_SANITIZERS")
   if(ARG_UNPARSED_ARGUMENTS OR ARG_KEYWORDS_MISSING_VALUES)
      message(FATAL_ERROR "Invalid vendored implementation policy arguments for ${target}")
   endif()

   if(NOT TARGET ${target})
      message(FATAL_ERROR "Unknown vendored implementation target: ${target}")
   endif()

   get_target_property(_forge_vendor_target_type ${target} TYPE)
   if(_forge_vendor_target_type STREQUAL "INTERFACE_LIBRARY")
      message(FATAL_ERROR "Vendored implementation policy requires a compiled target: ${target}")
   endif()

   set(_forge_vendor_debug_optimization_options)
   foreach(_forge_vendor_language C CXX)
      if(NOT CMAKE_${_forge_vendor_language}_COMPILER_LOADED)
         continue()
      endif()
      _forge_vendor_debug_optimization_flag(
         "${_forge_vendor_language}"
         "${CMAKE_${_forge_vendor_language}_COMPILER_FRONTEND_VARIANT}"
         _forge_vendor_optimization_flag
      )
      list(
         APPEND _forge_vendor_debug_optimization_options
         "$<$<AND:$<CONFIG:Debug>,$<COMPILE_LANGUAGE:${_forge_vendor_language}>>:${_forge_vendor_optimization_flag}>"
      )
   endforeach()

   # Target options follow parent configuration flags, so the final selector
   # optimizes vendor code without changing Forge Debug or sanitizer settings.
   target_compile_options(${target} PRIVATE ${_forge_vendor_debug_optimization_options})

   list(REMOVE_DUPLICATES ARG_DISABLED_SANITIZERS)
   foreach(_forge_vendor_disabled_sanitizer IN LISTS ARG_DISABLED_SANITIZERS)
      if(NOT _forge_vendor_disabled_sanitizer STREQUAL "alignment")
         message(FATAL_ERROR "Unsupported vendored sanitizer suppression: ${_forge_vendor_disabled_sanitizer}")
      endif()

      foreach(_forge_vendor_language C CXX)
         if(NOT CMAKE_${_forge_vendor_language}_COMPILER_LOADED)
            continue()
         endif()

         if(CMAKE_${_forge_vendor_language}_COMPILER_FRONTEND_VARIANT STREQUAL "GNU")
            set(_forge_vendor_flag "-fno-sanitize=alignment")
            set(_forge_vendor_flag_check "FORGE_${_forge_vendor_language}_HAS_FNO_SANITIZE_ALIGNMENT")
            check_compiler_flag(${_forge_vendor_language} "${_forge_vendor_flag}" ${_forge_vendor_flag_check})
            if(NOT ${_forge_vendor_flag_check})
               message(
                  FATAL_ERROR
                  "${CMAKE_${_forge_vendor_language}_COMPILER_ID} ${_forge_vendor_language} compiler does not support ${_forge_vendor_flag}"
               )
            endif()
            target_compile_options(
               ${target}
               PRIVATE "$<$<COMPILE_LANGUAGE:${_forge_vendor_language}>:${_forge_vendor_flag}>"
            )
         elseif(CMAKE_${_forge_vendor_language}_COMPILER_FRONTEND_VARIANT STREQUAL "MSVC")
            message(
               STATUS
               "Vendored alignment sanitizer suppression is not applicable to the MSVC-style ${_forge_vendor_language} frontend"
            )
         else()
            message(
               FATAL_ERROR
               "Unsupported ${_forge_vendor_language} compiler frontend for vendored alignment sanitizer suppression: ${CMAKE_${_forge_vendor_language}_COMPILER_FRONTEND_VARIANT}"
            )
         endif()
      endforeach()
   endforeach()

   set_property(TARGET ${target} PROPERTY FORGE_VENDORED_IMPLEMENTATION_POLICY ON)
   set_property(
      TARGET ${target}
      PROPERTY FORGE_VENDORED_IMPLEMENTATION_DISABLED_SANITIZERS "${ARG_DISABLED_SANITIZERS}"
   )
endfunction()
