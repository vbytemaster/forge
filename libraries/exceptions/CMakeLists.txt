add_library(fcl_exceptions STATIC exceptions.cpp)
fcl_target_modules(fcl_exceptions exceptions)

target_include_directories(fcl_exceptions PUBLIC ${FCL_PUBLIC_INCLUDE_DIRS})
target_link_libraries(
   fcl_exceptions PUBLIC
      fcl_core
)
target_compile_definitions(
   fcl_exceptions
   PUBLIC OPENSSL_API_COMPAT=0x30000000L
          OPENSSL_NO_DEPRECATED
)
