get_filename_component(FORGE_XML_PUGIXML_SOURCE_DIR "${CMAKE_CURRENT_LIST_DIR}/../../vendor/pugixml/src" ABSOLUTE)

add_library(
   forge_xml STATIC
   xml.cpp
   "${FORGE_XML_PUGIXML_SOURCE_DIR}/pugixml.cpp"
)
forge_target_modules(forge_xml xml)

target_include_directories(
   forge_xml
   PUBLIC ${FORGE_PUBLIC_INCLUDE_DIRS}
   PRIVATE "${FORGE_XML_PUGIXML_SOURCE_DIR}"
)
target_link_libraries(
   forge_xml PUBLIC
      forge_config
      forge_core
      forge_reflect
      forge_schema
)
target_compile_definitions(
   forge_xml
   PRIVATE
      PUGIXML_NO_XPATH
      PUGIXML_NO_EXCEPTIONS
)
