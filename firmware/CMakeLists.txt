cmake_minimum_required(VERSION 3.16)
include($ENV{IDF_PATH}/tools/cmake/project.cmake)
# The app's version string: the nearest v* tag (not the hardware checkpoint tags), the commits since it,
# the hash, and -dirty for an unclean tree. Without a v* tag it is the bare hash.
execute_process(COMMAND git describe --tags --match "v*" --always --dirty
                WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR} OUTPUT_VARIABLE PROJECT_VER
                OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET)
if(NOT PROJECT_VER)
    set(PROJECT_VER "unknown")
endif()
project(samplooler)
