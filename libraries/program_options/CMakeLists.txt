add_library(forge_program_options STATIC program_options.cpp)
forge_target_modules(forge_program_options program_options)

target_include_directories(forge_program_options PUBLIC ${FORGE_PUBLIC_INCLUDE_DIRS})

target_link_libraries(
   forge_program_options PUBLIC
      forge_config
      forge_schema
      Boost::program_options
)
