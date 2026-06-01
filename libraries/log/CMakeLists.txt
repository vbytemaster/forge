add_library(
   fcl_log STATIC
   appender.cpp
   console_appender.cpp
   record.cpp
   log_message.cpp
   logger.cpp
   logger_config.cpp
)
fcl_target_modules(fcl_log log)

target_include_directories(fcl_log PUBLIC ${FCL_PUBLIC_INCLUDE_DIRS})
target_link_libraries(
   fcl_log PUBLIC
      fcl_core
      fcl_exceptions
      fcl_reflect
      fcl_variant
      Boost::headers
   PRIVATE
      Boost::dll
)
if(FCL_HAS_STD_STACKTRACE)
   target_compile_definitions(fcl_log PRIVATE FCL_HAS_STD_STACKTRACE=1)
elseif(TARGET Boost::stacktrace_basic)
   target_compile_definitions(
      fcl_log PRIVATE
         FCL_HAS_BOOST_STACKTRACE=1
         BOOST_STACKTRACE_GNU_SOURCE_NOT_REQUIRED
   )
   target_link_libraries(fcl_log PRIVATE Boost::stacktrace_basic)
endif()
target_compile_definitions(
   fcl_log
   PUBLIC OPENSSL_API_COMPAT=0x30000000L
          OPENSSL_NO_DEPRECATED
)
