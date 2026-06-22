file(MAKE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/generated")
configure_file(
   "${CMAKE_CURRENT_SOURCE_DIR}/git_revision.cpp.in"
   "${CMAKE_CURRENT_BINARY_DIR}/generated/git_revision.cpp"
   @ONLY
)

add_library(
   fcl_core STATIC
   chrono.cpp
   encoding.cpp
   string.cpp
   uint128.cpp
   utf8.cpp
   "${CMAKE_CURRENT_BINARY_DIR}/generated/git_revision.cpp"
)
fcl_target_modules(fcl_core core)

target_include_directories(
   fcl_core
   PUBLIC ${FCL_PUBLIC_INCLUDE_DIRS}
   PRIVATE "${PROJECT_SOURCE_DIR}/vendor/utf8cpp"
)
target_link_libraries(
   fcl_core PRIVATE
      Boost::headers
      Boost::date_time
      Boost::multiprecision
)
target_compile_definitions(
   fcl_core
   PUBLIC OPENSSL_API_COMPAT=0x30000000L
          OPENSSL_NO_DEPRECATED
)
