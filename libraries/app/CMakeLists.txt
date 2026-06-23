add_library(
   forge_app STATIC
   application.cpp
   events.cpp
   diagnostics.cpp
   signals.cpp
   plugin_context.cpp
   plugin.cpp
   plugin_registry.cpp
   application_shell.cpp
   application_builder.cpp
   runner.cpp
   daemon.cpp
)
forge_target_modules(forge_app app)

target_include_directories(forge_app PUBLIC ${FORGE_PUBLIC_INCLUDE_DIRS})

target_link_libraries(
   forge_app PUBLIC
      forge_exceptions
      forge_api
      forge_asio
      forge_config
      forge_env
      forge_program_options
      forge_yaml
      Boost::headers
)
