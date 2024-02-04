string(TIMESTAMP date "%Y%m%d")

if(NOT EXISTS "${OUTPUT_DIR}/gamedb${SYSTEM}_${date}.o")

find_package (Python COMPONENTS Interpreter)

file(GLOB files "${OUTPUT_DIR}/gamedb${SYSTEM}_*")
foreach(file ${files})
file(REMOVE "${file}")
endforeach()


set(GAMEDB_${SYSTEM}_BIN "gamedb${SYSTEM}.dat")
set(GAMEDB_${SYSTEM}_OBJ "${OUTPUT_DIR}/gamedb${SYSTEM}_${date}.o")

execute_process(
    COMMAND ${Python_EXECUTABLE} ${PYTHON_SCRIPT} ${SYSTEM} ${OUTPUT_DIR}
    WORKING_DIRECTORY ${OUTPUT_DIR}
    OUTPUT_QUIET
)
execute_process(
    COMMAND ${CMAKE_OBJCOPY} --input-target=binary --output-target=elf32-littlearm --binary-architecture arm --rename-section .data=.rodata "${GAMEDB_${SYSTEM}_BIN}" "${GAMEDB_${SYSTEM}_OBJ}"
    WORKING_DIRECTORY ${OUTPUT_DIR}
    OUTPUT_QUIET
)

file(CREATE_LINK ${GAMEDB_${SYSTEM}_OBJ} "${OUTPUT_DIR}/gamedb${SYSTEM}.o" SYMBOLIC)
file(REMOVE_RECURSE "${OUTPUT_DIR}/${SYSTEM}")

endif()