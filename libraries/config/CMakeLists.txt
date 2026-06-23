add_library(
   forge_config STATIC
   key_path.cpp
   document.cpp
   component.cpp
   decode.cpp
   migration.cpp
   config.cpp
)
forge_target_modules(forge_config config)

target_include_directories(forge_config PUBLIC ${FORGE_PUBLIC_INCLUDE_DIRS})

target_link_libraries(
   forge_config PUBLIC
      forge_schema
)
