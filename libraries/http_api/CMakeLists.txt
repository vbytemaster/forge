add_library(forge_http_api STATIC)
forge_target_modules(forge_http_api http_api)

target_include_directories(forge_http_api PUBLIC ${FORGE_PUBLIC_INCLUDE_DIRS})

target_link_libraries(
   forge_http_api PUBLIC
      forge_api
      forge_http
      forge_json
      forge_xml
      forge_raw
      forge_reflect
      forge_schema
      Boost::asio
)
