cmake_minimum_required(VERSION 3.12)

find_package(Git QUIET)

if(Git_FOUND AND EXISTS "${SCRIPT_WORKING_DIR}/../../.git")
    # A build can use local tags when origin is unavailable.
    execute_process(COMMAND ${GIT_EXECUTABLE} fetch origin --tags --force
                    WORKING_DIRECTORY ${SCRIPT_WORKING_DIR}
                    OUTPUT_QUIET ERROR_QUIET)
    execute_process(COMMAND ${GIT_EXECUTABLE} describe --tags --exact-match HEAD --exclude=latest --exclude=nightly
                    OUTPUT_VARIABLE SD2PSX_VERSION WORKING_DIRECTORY ${SCRIPT_WORKING_DIR}
                    RESULT_VARIABLE git_tag_result
                    OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET)
    execute_process(COMMAND ${GIT_EXECUTABLE} rev-parse --short HEAD
                    OUTPUT_VARIABLE SD2PSX_COMMIT WORKING_DIRECTORY ${SCRIPT_WORKING_DIR}
                    RESULT_VARIABLE git_commit_result
                    OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET)
    execute_process(COMMAND ${GIT_EXECUTABLE} rev-parse --abbrev-ref HEAD
                    OUTPUT_VARIABLE SD2PSX_BRANCH WORKING_DIRECTORY ${SCRIPT_WORKING_DIR}
                    RESULT_VARIABLE git_branch_result
                    OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET)

    if(NOT git_commit_result STREQUAL "0")
        set(SD2PSX_VERSION "None")
        set(SD2PSX_COMMIT "None")
    elseif(NOT git_tag_result STREQUAL "0")
        set(SD2PSX_VERSION "nightly")
    endif()
    if(NOT git_branch_result STREQUAL "0")
        set(SD2PSX_BRANCH "None")
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
string(REPLACE "@VARIANT@" ${VARIANT} template_file "${template_file}")

file(WRITE ${SCRIPT_OUTPUT_FILE} "${template_file}")
