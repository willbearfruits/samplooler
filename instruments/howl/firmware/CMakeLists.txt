cmake_minimum_required(VERSION 3.16)
include($ENV{IDF_PATH}/tools/cmake/project.cmake)
# The version string: the entry's own (../entry.json), as the library and the app show it.
file(READ ${CMAKE_CURRENT_SOURCE_DIR}/../entry.json ENTRY_JSON)
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/../entry.json)   # a new version re-runs CMake
string(JSON PROJECT_VER ERROR_VARIABLE entry_err GET "${ENTRY_JSON}" version)
if(entry_err OR NOT PROJECT_VER)
    set(PROJECT_VER "0.1.0")
endif()
project(howl)
