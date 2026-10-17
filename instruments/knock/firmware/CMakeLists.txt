cmake_minimum_required(VERSION 3.16)
include($ENV{IDF_PATH}/tools/cmake/project.cmake)
# The version string: the nearest v* tag if this folder is ever a git repo, else 0.1.0.
execute_process(COMMAND git describe --tags --match "v*" --always --dirty
                WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR} OUTPUT_VARIABLE PROJECT_VER
                OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET)
if(NOT PROJECT_VER)
    set(PROJECT_VER "0.1.0")
endif()
project(knock)
