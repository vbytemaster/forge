add_library(
   fcl_plugins_signing_provider STATIC
      plugin.cpp
      signing_api.cpp
      config.cpp
      plugin_impl.cpp
)
fcl_target_modules_at(fcl_plugins_signing_provider plugins/signing/provider)

target_include_directories(
   fcl_plugins_signing_provider
   PUBLIC
      ${FCL_PUBLIC_INCLUDE_DIRS}
      "$<BUILD_INTERFACE:${FCL_PLUGINS_SIGNING_PROVIDER_INCLUDE_DIR}>"
   PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(
   fcl_plugins_signing_provider PUBLIC
      fcl_app
      fcl_api
      fcl_config
      fcl_schema
      fcl_crypto
      fcl_exceptions
      Boost::asio
)
