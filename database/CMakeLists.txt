# PS1
set(GAMEDB_PS1_OBJ "${CMAKE_CURRENT_BINARY_DIR}/gamedbps1.o")

add_custom_target(gamedbobjs_ps1 ALL
                    COMMAND ${CMAKE_COMMAND}
                        -D OUTPUT_DIR=${CMAKE_CURRENT_BINARY_DIR}
                        -D PYTHON_SCRIPT=${CMAKE_CURRENT_SOURCE_DIR}/parse_GameDB.py
                        -D CMAKE_OBJCOPY=${CMAKE_OBJCOPY}
                        -D SYSTEM=ps1
                        -D "INPUT_URLS=https://github.com/niemasd/GameDB-PSX/releases/latest/download/PSX.data.json"
                        -D "REPO_ROOT=${CMAKE_SOURCE_DIR}"
                        -P ${CMAKE_CURRENT_SOURCE_DIR}/db_obj_builder.cmake
                    VERBATIM
                    BYPRODUCTS ${GAMEDB_PS1_OBJ})

add_library(gamedb INTERFACE)
add_dependencies(gamedb gamedbobjs_ps1)

target_link_libraries(gamedb INTERFACE ${GAMEDB_PS1_OBJ})

# PS2

set(GAMEDB_PS2_OBJ "${CMAKE_CURRENT_BINARY_DIR}/gamedbps2.o")

add_custom_target(gamedbobjs_ps2 ALL
                    COMMAND ${CMAKE_COMMAND}
                        -D OUTPUT_DIR=${CMAKE_CURRENT_BINARY_DIR}
                        -D PYTHON_SCRIPT=${CMAKE_CURRENT_SOURCE_DIR}/get_and_parse_hdldb.py
                        -D CMAKE_OBJCOPY=${CMAKE_OBJCOPY}
                        -D SYSTEM=ps2
                        -D "INPUT_URLS=https://github.com/israpps/HDL-Batch-installer/raw/main/Database/gamename.csv"
                        -D "REPO_ROOT=${CMAKE_SOURCE_DIR}"
                        -P ${CMAKE_CURRENT_SOURCE_DIR}/db_obj_builder.cmake
                    VERBATIM
                    BYPRODUCTS ${GAMEDB_PS2_OBJ})

add_dependencies(gamedb gamedbobjs_ps2)

target_link_libraries(gamedb INTERFACE ${GAMEDB_PS2_OBJ})

# PS2

set(GAMEDB_COH_OBJ "${CMAKE_CURRENT_BINARY_DIR}/gamedbcoh.o")

add_custom_target(gamedbobjs_coh ALL
                    COMMAND ${CMAKE_COMMAND}
                        -D OUTPUT_DIR=${CMAKE_CURRENT_BINARY_DIR}
                        -D PYTHON_SCRIPT=${CMAKE_CURRENT_SOURCE_DIR}/parse_arcade.py
                        -D CMAKE_OBJCOPY=${CMAKE_OBJCOPY}
                        -D SYSTEM=coh
                        -D "INPUT_URLS=https://raw.githubusercontent.com/israpps/AthenaEnv/refs/heads/aclauncher/bin/aclauncher/246.json;https://raw.githubusercontent.com/israpps/AthenaEnv/refs/heads/aclauncher/bin/aclauncher/256.json"
                        -D "REPO_ROOT=${CMAKE_SOURCE_DIR}"
                        -P ${CMAKE_CURRENT_SOURCE_DIR}/db_obj_builder.cmake
                    VERBATIM
                    BYPRODUCTS ${GAMEDB_COH_OBJ})

add_dependencies(gamedb gamedbobjs_coh)

target_link_libraries(gamedb INTERFACE ${GAMEDB_COH_OBJ})
