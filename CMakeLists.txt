cmake_minimum_required(VERSION 3.25)

# ── Bootstrap vcpkg if needed ────────────────────────────────────────────────
if(NOT DEFINED CMAKE_TOOLCHAIN_FILE)
    find_package(Git QUIET)
    set(VCPKG_DIR ${CMAKE_SOURCE_DIR}/vcpkg)
    if(NOT EXISTS ${VCPKG_DIR})
        message(STATUS "Cloning vcpkg into ${VCPKG_DIR}...")
        execute_process(
            COMMAND ${GIT_EXECUTABLE} clone https://github.com/microsoft/vcpkg.git ${VCPKG_DIR}
            RESULT_VARIABLE GIT_RESULT
        )
        if(NOT GIT_RESULT EQUAL 0)
            message(FATAL_ERROR "Failed to clone vcpkg. Install it manually or set CMAKE_TOOLCHAIN_FILE.")
        endif()
    endif()
    if(WIN32)
        set(VCPKG_BOOTSTRAP ${VCPKG_DIR}/bootstrap-vcpkg.bat)
        set(VCPKG_EXECUTABLE ${VCPKG_DIR}/vcpkg.exe)
    else()
        set(VCPKG_BOOTSTRAP ${VCPKG_DIR}/bootstrap-vcpkg.sh)
        set(VCPKG_EXECUTABLE ${VCPKG_DIR}/vcpkg)
    endif()
    if(NOT EXISTS ${VCPKG_EXECUTABLE})
        message(STATUS "Bootstrapping vcpkg...")
        execute_process(COMMAND ${VCPKG_BOOTSTRAP} -disableMetrics WORKING_DIRECTORY ${VCPKG_DIR})
    endif()
    set(CMAKE_TOOLCHAIN_FILE ${VCPKG_DIR}/scripts/buildsystems/vcpkg.cmake CACHE FILEPATH "")
endif()

project(pdb-browser LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# CMake doesn't know about C++26 yet — use /std:c++latest on MSVC
add_compile_options($<$<CXX_COMPILER_ID:MSVC>:/std:c++latest>)

# ── vcpkg packages ──────────────────────────────────────────────────────────
find_package(SDL3 CONFIG REQUIRED)
find_package(imgui CONFIG REQUIRED)
find_package(unofficial-imgui-node-editor CONFIG REQUIRED)
find_package(nlohmann_json CONFIG REQUIRED)
find_package(OpenGL REQUIRED)

# ── DIA SDK ─────────────────────────────────────────────────────────────────
include(cmake/FindDIASDK.cmake)

# ── pdb-browser executable ───────────────────────────────────────────────────
file(GLOB_RECURSE SOURCES CONFIGURE_DEPENDS src/*.cpp)
file(GLOB_RECURSE HEADERS  src/*.h)

add_executable(pdb-browser WIN32 ${SOURCES})

target_sources(pdb-browser PRIVATE ${HEADERS})
source_group(  
    TREE ${CMAKE_CURRENT_SOURCE_DIR}/src
    PREFIX "Headers"
    FILES ${HEADERS}
)
source_group(  
    TREE ${CMAKE_CURRENT_SOURCE_DIR}/src
    PREFIX "Source Files"
    FILES ${SOURCES}
)

target_include_directories(pdb-browser PRIVATE
    src/
    ${DIASDK_INCLUDE_DIR}
)

target_link_libraries(pdb-browser PRIVATE
    imgui::imgui
    unofficial::imgui-node-editor::imgui-node-editor
    SDL3::SDL3
    nlohmann_json::nlohmann_json
    OpenGL::GL
    ${DIASDK_LIBRARY}
    ole32
    oleaut32
    uuid
)

target_compile_options(pdb-browser PRIVATE
    $<$<CXX_COMPILER_ID:MSVC>:/W4 /WX /permissive->
)

# Copy DIA DLL next to the executable so it can be found at runtime
add_custom_command(TARGET pdb-browser POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        ${DIASDK_DLL}
        $<TARGET_FILE_DIR:pdb-browser>
    COMMENT "Copying msdia140.dll"
)
