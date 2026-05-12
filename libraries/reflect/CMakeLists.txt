add_library(fcl_reflect STATIC reflect.cpp)
fcl_target_modules(fcl_reflect reflect)
target_include_directories(fcl_reflect PUBLIC ${FCL_PUBLIC_INCLUDE_DIRS})
target_link_libraries(
   fcl_reflect PUBLIC
      fcl_core
      Boost::headers
)
