add_library(fcl_http_api STATIC http_api.cpp)
fcl_target_modules(fcl_http_api http_api)

target_include_directories(fcl_http_api PUBLIC ${FCL_PUBLIC_INCLUDE_DIRS})

target_link_libraries(
   fcl_http_api PUBLIC
      fcl_api
      fcl_http
      fcl_json
      fcl_raw
      fcl_reflect
      fcl_schema
      Boost::asio
)
