string(TIMESTAMP date "%Y%m%d")

set(GAMEDB_${SYSTEM}_BIN "gamedb${SYSTEM}.dat")
set(GAMEDB_${SYSTEM}_OBJ "${OUTPUT_DIR}/gamedb${SYSTEM}_${date}.o")

if(NOT EXISTS "${GAMEDB_${SYSTEM}_OBJ}")

find_package (Python3 COMPONENTS Interpreter)
execute_process (COMMAND "${Python3_EXECUTABLE}" -m venv "${OUTPUT_DIR}/db_builder")

# Here is the trick
## update the environment with VIRTUAL_ENV variable (mimic the activate script)
set (ENV{VIRTUAL_ENV} "${OUTPUT_DIR}/db_builder")
## change the context of the search
set (Python3_FIND_VIRTUALENV FIRST)
## unset Python3_EXECUTABLE because it is also an input variable (see documentation, Artifacts Specification section)
unset (Python3_EXECUTABLE)
## Launch a new search
find_package (Python3 COMPONENTS Interpreter Development)

file(GLOB files "${OUTPUT_DIR}/gamedb${SYSTEM}_*")
foreach(file ${files})
file(REMOVE "${file}")
endforeach()

execute_process(
    COMMAND ${Python3_EXECUTABLE} -m pip install requests unidecode
    WORKING_DIRECTORY ${OUTPUT_DIR}
    OUTPUT_QUIET
)
execute_process(
    COMMAND ${Python3_EXECUTABLE} ${PYTHON_SCRIPT} ${SYSTEM} ${OUTPUT_DIR}
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