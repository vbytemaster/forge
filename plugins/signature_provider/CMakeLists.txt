add_library(
   fcl_plugin_signature_provider STATIC
      plugin.cpp
      api_facade.cpp
      config.cpp
      state.cpp
)
fcl_target_modules_at(fcl_plugin_signature_provider plugins/signature_provider)

target_include_directories(
   fcl_plugin_signature_provider
   PUBLIC
      ${FCL_PUBLIC_INCLUDE_DIRS}
      "$<BUILD_INTERFACE:${FCL_PLUGIN_SIGNATURE_PROVIDER_INCLUDE_DIR}>"
   PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(
   fcl_plugin_signature_provider PUBLIC
      fcl_app
      fcl_api
      fcl_config
      fcl_schema
      fcl_crypto
      fcl_exceptions
      Boost::asio
)
