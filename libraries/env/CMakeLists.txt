add_library(forge_env STATIC env.cpp)
forge_target_modules(forge_env env)

target_include_directories(forge_env PUBLIC ${FORGE_PUBLIC_INCLUDE_DIRS})
target_link_libraries(
   forge_env PUBLIC
      forge_config
      forge_schema
)
