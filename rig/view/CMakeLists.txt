cmake_minimum_required(VERSION 3.24)
project(slrig_view VERSION 0.1.0 LANGUAGES C CXX)

# slrig-view, the Samplooler Rig's viewer: GLFW + OpenGL 3.3 core + NanoVG, offscreen shots through EGL.
#   cmake -S rig/view -B rig/view/build -G Ninja -DCMAKE_BUILD_TYPE=Release && cmake --build rig/view/build

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "" FORCE)
endif()
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

find_package(glfw3 3.3 REQUIRED)
find_package(OpenGL REQUIRED COMPONENTS EGL)
find_package(Threads REQUIRED)
find_package(nlohmann_json 3.11 REQUIRED)

# vendored C: glad (GL 3.3 core loader), NanoVG (with stb_truetype and stb_image), stb_image_write
add_library(slrig_view_ext STATIC
    external/glad/src/gl.c
    external/nanovg/nanovg.c
    src/ext_impl.c)
target_include_directories(slrig_view_ext SYSTEM PUBLIC
    external/glad/include
    external/nanovg
    external/stb)
target_compile_options(slrig_view_ext PRIVATE -w)
target_link_libraries(slrig_view_ext PUBLIC m ${CMAKE_DL_LIBS})

add_executable(slrig-view
    src/main.cpp
    src/App.cpp
    src/Panels.cpp
    src/Plots.cpp
    src/Scene3D.cpp
    src/Ui.cpp
    src/PxFont.cpp
    src/Model.cpp
    src/Net.cpp)
target_compile_options(slrig-view PRIVATE -Wall -Wextra)
target_compile_definitions(slrig-view PRIVATE SLRIG_VIEW_ASSETS="${CMAKE_CURRENT_SOURCE_DIR}/assets")
target_link_libraries(slrig-view PRIVATE
    slrig_view_ext
    glfw
    OpenGL::EGL
    nlohmann_json::nlohmann_json
    Threads::Threads)
