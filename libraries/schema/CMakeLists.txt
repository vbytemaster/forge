add_library(forge_schema STATIC)
forge_target_modules(forge_schema schema)

target_include_directories(forge_schema PUBLIC ${FORGE_PUBLIC_INCLUDE_DIRS})

target_link_libraries(
   forge_schema PUBLIC
      forge_reflect
      Boost::headers
)
