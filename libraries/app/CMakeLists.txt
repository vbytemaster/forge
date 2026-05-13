add_library(
   fcl_app STATIC
   application.cpp
   ports.cpp
   events.cpp
   diagnostics.cpp
   signals.cpp
   plugin_context.cpp
   plugin.cpp
   plugin_registry.cpp
   application_shell.cpp
   application_builder.cpp
   runner.cpp
)
fcl_target_modules(fcl_app app)

target_include_directories(fcl_app PUBLIC ${FCL_PUBLIC_INCLUDE_DIRS})

target_link_libraries(
   fcl_app PUBLIC
      fcl_asio
      fcl_config
      Boost::headers
)
