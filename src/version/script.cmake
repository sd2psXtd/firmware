cmake_minimum_required(VERSION 3.12)

find_package(Git QUIET)

if(Git_FOUND AND EXISTS "${SCRIPT_WORKING_DIR}/../../.git")
    execute_process(COMMAND ${GIT_EXECUTABLE} describe --tags --exact-match HEAD --exclude=latest --exclude=nightly
                    OUTPUT_VARIABLE SD2PSX_VERSION WORKING_DIRECTORY ${SCRIPT_WORKING_DIR}
                    OUTPUT_STRIP_TRAILING_WHITESPACE)
    execute_process(COMMAND ${GIT_EXECUTABLE} rev-parse --short HEAD
                    OUTPUT_VARIABLE SD2PSX_COMMIT WORKING_DIRECTORY ${SCRIPT_WORKING_DIR}
                    OUTPUT_STRIP_TRAILING_WHITESPACE)
    execute_process(COMMAND ${GIT_EXECUTABLE} rev-parse --abbrev-ref HEAD
                    OUTPUT_VARIABLE SD2PSX_BRANCH WORKING_DIRECTORY ${SCRIPT_WORKING_DIR}
                    OUTPUT_STRIP_TRAILING_WHITESPACE)
    string(REGEX REPLACE "\n$" "" SD2PSX_VERSION "${SD2PSX_VERSION}")
    string(REGEX REPLACE "\n$" "" SD2PSX_COMMIT "${SD2PSX_COMMIT}")
    string(REGEX REPLACE "\n$" "" SD2PSX_BRANCH "${SD2PSX_BRANCH}")
    if("${SD2PSX_VERSION}" STREQUAL "")
        set(SD2PSX_VERSION "nightly")
    endif()
else()
    set(SD2PSX_VERSION "None")
    set(SD2PSX_COMMIT "None")
    set(SD2PSX_BRANCH "None")
endif()

file(READ ${SCRIPT_TEMPLATE} template_file)

string(REPLACE "@SD2PSX_VERSION@" ${SD2PSX_VERSION} template_file "${template_file}")
string(REPLACE "@SD2PSX_COMMIT@" ${SD2PSX_COMMIT} template_file "${template_file}")
string(REPLACE "@SD2PSX_BRANCH@" ${SD2PSX_BRANCH} template_file "${template_file}")

file(WRITE ${SCRIPT_OUTPUT_FILE} "${template_file}")
