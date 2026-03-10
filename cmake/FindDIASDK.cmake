# FindDIASDK.cmake
# Locates the Microsoft DIA SDK shipped with Visual Studio.
#
# Exports:
#   DIASDK_FOUND
#   DIASDK_INCLUDE_DIR
#   DIASDK_LIBRARY
#   DIASDK_DLL

# Allow override via env var or CMake cache
if(DEFINED ENV{DIA_SDK_DIR})
    set(_DIA_HINT "$ENV{DIA_SDK_DIR}")
endif()

# Walk known VS install paths via vswhere
if(NOT _DIA_HINT)
    set(_PFx86 "C:/Program Files (x86)")
    if(DEFINED ENV{ProgramFiles\(x86\)})
        set(_PFx86 "$ENV{ProgramFiles\(x86\)}")
    endif()
    find_program(_VSWHERE vswhere
        PATHS
            "${_PFx86}/Microsoft Visual Studio/Installer"
            "$ENV{ProgramFiles}/Microsoft Visual Studio/Installer"
    )
    if(_VSWHERE)
        execute_process(
            COMMAND ${_VSWHERE} -latest -property installationPath
            OUTPUT_VARIABLE _VS_INSTALL
            OUTPUT_STRIP_TRAILING_WHITESPACE
        )
        if(_VS_INSTALL)
            set(_DIA_HINT "${_VS_INSTALL}/DIA SDK")
        endif()
    endif()
endif()

find_path(DIASDK_INCLUDE_DIR
    NAMES dia2.h
    HINTS "${_DIA_HINT}/include"
    DOC "DIA SDK include directory"
)

find_library(DIASDK_LIBRARY
    NAMES diaguids
    HINTS "${_DIA_HINT}/lib/amd64"
    DOC "DIA SDK import library"
)

# The DIA DLL is not in lib/ — it lives in the VS IDE directory
find_file(DIASDK_DLL
    NAMES msdia140.dll
    HINTS
        "${_DIA_HINT}/../IDE"         # VS2022 typical location
        "${_DIA_HINT}/bin/amd64"
    DOC "DIA SDK runtime DLL"
)

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(DIASDK
    REQUIRED_VARS DIASDK_INCLUDE_DIR DIASDK_LIBRARY
    FAIL_MESSAGE "DIA SDK not found. Set DIA_SDK_DIR env var to the DIA SDK root (e.g. C:/Program Files/Microsoft Visual Studio/2022/Community/DIA SDK)"
)

mark_as_advanced(DIASDK_INCLUDE_DIR DIASDK_LIBRARY DIASDK_DLL)
