add_library(
   fcl_config STATIC
   key_path.cpp
   document.cpp
   component.cpp
   decode.cpp
   config.cpp
)
fcl_target_modules(fcl_config config)

target_include_directories(fcl_config PUBLIC ${FCL_PUBLIC_INCLUDE_DIRS})

target_link_libraries(
   fcl_config PUBLIC
      fcl_schema
)
