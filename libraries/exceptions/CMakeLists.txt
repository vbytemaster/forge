add_library(forge_exceptions STATIC exceptions.cpp)
forge_target_modules(forge_exceptions exceptions)

target_include_directories(forge_exceptions PUBLIC ${FORGE_PUBLIC_INCLUDE_DIRS})
target_link_libraries(
   forge_exceptions PUBLIC
      forge_core
)
target_compile_definitions(
   forge_exceptions
   PUBLIC OPENSSL_API_COMPAT=0x30000000L
          OPENSSL_NO_DEPRECATED
)
