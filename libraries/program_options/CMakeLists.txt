add_library(fcl_program_options STATIC program_options.cpp)
fcl_target_modules(fcl_program_options program_options)

target_include_directories(fcl_program_options PUBLIC ${FCL_PUBLIC_INCLUDE_DIRS})

target_link_libraries(
   fcl_program_options PUBLIC
      fcl_config
      fcl_schema
      Boost::program_options
)
