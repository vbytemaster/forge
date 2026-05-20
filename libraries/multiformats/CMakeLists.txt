file(GLOB FCL_MULTIFORMATS_SOURCES CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/*.cpp")

add_library(fcl_multiformats STATIC ${FCL_MULTIFORMATS_SOURCES})
fcl_target_modules(fcl_multiformats multiformats)

target_include_directories(fcl_multiformats PUBLIC ${FCL_PUBLIC_INCLUDE_DIRS})

target_link_libraries(
   fcl_multiformats PUBLIC
      fcl_exception
      fcl_crypto
)
