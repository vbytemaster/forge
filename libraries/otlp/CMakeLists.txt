add_library(
   fcl_otlp STATIC
   log_exporter.cpp
   log_sink.cpp
)
fcl_target_modules(fcl_otlp otlp)

target_include_directories(fcl_otlp PUBLIC ${FCL_PUBLIC_INCLUDE_DIRS})

target_link_libraries(
   fcl_otlp PUBLIC
      fcl_exceptions
      fcl_log
      fcl_asio
      fcl_http
)
