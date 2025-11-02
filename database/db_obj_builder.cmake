string(TIMESTAMP date "%Y%m%d")

set(GAMEDB_${SYSTEM}_IN "gamedb${SYSTEM}_input")
set(GAMEDB_${SYSTEM}_BIN "gamedb${SYSTEM}.dat")
set(GAMEDB_${SYSTEM}_OBJ "${OUTPUT_DIR}/gamedb${SYSTEM}_${date}.o")

if(NOT EXISTS "${GAMEDB_${SYSTEM}_OBJ}")

find_package (Python3 COMPONENTS Interpreter Development)

file(GLOB files "${OUTPUT_DIR}/gamedb${SYSTEM}_*")
foreach(file ${files})
file(REMOVE "${file}")
endforeach()
file(MAKE_DIRECTORY ${GAMEDB_${SYSTEM}_IN})
foreach(url ${INPUT_URLS})
get_filename_component(url_BASENAME ${url} NAME)
file(DOWNLOAD "${url}" "${GAMEDB_${SYSTEM}_IN}/${url_BASENAME}")
endforeach()
execute_process(
    COMMAND ${CMAKE_COMMAND} -E env "PYTHONPATH=${REPO_ROOT}/ext/unidecode" ${Python3_EXECUTABLE} ${PYTHON_SCRIPT} ${SYSTEM} ${OUTPUT_DIR} "${GAMEDB_${SYSTEM}_IN}" "${GAMEDB_${SYSTEM}_BIN}"
    WORKING_DIRECTORY ${OUTPUT_DIR}
    OUTPUT_QUIET
)
execute_process(
    COMMAND ${CMAKE_OBJCOPY} --input-target=binary --output-target=elf32-littlearm --binary-architecture arm --rename-section .data=.rodata "${GAMEDB_${SYSTEM}_BIN}" "${GAMEDB_${SYSTEM}_OBJ}"
    WORKING_DIRECTORY ${OUTPUT_DIR}
    OUTPUT_QUIET
)

file(REMOVE_RECURSE "${OUTPUT_DIR}/${SYSTEM}")

endif()

file(CREATE_LINK ${GAMEDB_${SYSTEM}_OBJ} "${OUTPUT_DIR}/gamedb${SYSTEM}.o" SYMBOLIC)