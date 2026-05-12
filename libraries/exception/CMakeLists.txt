add_library(fcl_exception STATIC exception.cpp)
fcl_target_modules(fcl_exception exception)

target_include_directories(fcl_exception PUBLIC ${FCL_PUBLIC_INCLUDE_DIRS})
target_link_libraries(
   fcl_exception PUBLIC
      fcl_core
)
target_compile_definitions(
   fcl_exception
   PUBLIC OPENSSL_API_COMPAT=0x30000000L
          OPENSSL_NO_DEPRECATED
)
