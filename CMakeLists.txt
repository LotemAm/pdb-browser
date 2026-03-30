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
find_package(nlohmann_json CONFIG REQUIRED)
find_package(OpenGL REQUIRED)

# ── imnodes (fetched at configure time) ─────────────────────────────────────
include(FetchContent)
FetchContent_Declare(
    imnodes
    GIT_REPOSITORY https://github.com/Nelarius/imnodes.git
    GIT_TAG        b2ec254ce576ac3d42dfb7aef61deadbff8e7211  # 2025-06-25
)
set(IMNODES_EXAMPLES OFF CACHE BOOL "" FORCE)
set(IMNODES_IMGUI_TARGET_NAME imgui::imgui CACHE STRING "" FORCE)
FetchContent_MakeAvailable(imnodes)

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
    imnodes
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

# ── Tests ────────────────────────────────────────────────────────────────────
option(ENABLE_TESTS "Build unit tests" OFF)
if(ENABLE_TESTS)
    enable_testing()
    find_package(GTest CONFIG REQUIRED)

    add_executable(pdb-browser-tests
        tests/PrettifyTests.cpp
        tests/PdbIndexTests.cpp
        tests/ExporterTests.cpp
        tests/CodeGenTests.cpp
        # Testable source modules (no DIA/SDL/ImGui dependency)
        src/pdb/Prettify.cpp
        src/pdb/PrettifyCpp.cpp
        src/pdb/PrettifyRust.cpp
        src/pdb/PdbIndex.cpp
        src/export/Exporter.cpp
        src/codegen/CodeGen.cpp
        src/codegen/CodeGenC.cpp
        src/codegen/CodeGenCpp.cpp
        src/codegen/CodeGenRust.cpp
    )

    target_include_directories(pdb-browser-tests PRIVATE src/)

    target_link_libraries(pdb-browser-tests PRIVATE
        GTest::gtest_main
        nlohmann_json::nlohmann_json
    )

    target_compile_options(pdb-browser-tests PRIVATE
        $<$<CXX_COMPILER_ID:MSVC>:/W4 /WX /permissive->
    )

    include(GoogleTest)
    gtest_discover_tests(pdb-browser-tests)
endif()
