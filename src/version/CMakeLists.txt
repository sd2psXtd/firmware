add_custom_target(sd2psx_version_generator ALL 
                    COMMAND ${CMAKE_COMMAND} 
                        -D SCRIPT_TEMPLATE=${CMAKE_CURRENT_SOURCE_DIR}/template/version.c 
                        -D SCRIPT_WORKING_DIR=${CMAKE_CURRENT_SOURCE_DIR}
                        -D SCRIPT_OUTPUT_FILE=${CMAKE_CURRENT_BINARY_DIR}/version.c
                        -P ${CMAKE_CURRENT_SOURCE_DIR}/script.cmake
                    BYPRODUCTS ${CMAKE_CURRENT_BINARY_DIR}/version.c)

add_library(sd2psx_version STATIC ${CMAKE_CURRENT_BINARY_DIR}/version.c)
target_include_directories(sd2psx_version PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
