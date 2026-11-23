cmake_minimum_required(VERSION 3.21)
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>" CACHE STRING "")
# LilithWidget — energy / level bar for Children of Lilith (Succubus of Skyrim),
# rendered through SKSE Menu Framework. Sibling of ArousedWidget (same skeleton).
project(LilithWidget VERSION 0.1.7 LANGUAGES CXX)

if(DEFINED ENV{SKYRIM_MODS_FOLDER} AND IS_DIRECTORY "$ENV{SKYRIM_MODS_FOLDER}")
    set(OUTPUT_FOLDER "$ENV{SKYRIM_MODS_FOLDER}/Lilith Widget--Claude")
endif()

# 0.1.7: CommonLibSSE-NG from alandtse (github.com/alandtse/CommonLibSSE-NG, branch `ng`), built from source out of
# the pinned submodule extern/CommonLibSSE-NG (tag v9.1.0), same as Minimal Vitals / Aroused Widget. It reads the
# Address Library v5 database and sets the AddressLibraryV5 flag, so 1.7.x SKSE loads the DLL. Up to 0.1.6 the vcpkg
# commonlibsse-ng 3.7.0 port was used, which 1.7 SKSE rejects.
set(CLIB_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/extern/CommonLibSSE-NG")
if(NOT EXISTS "${CLIB_SOURCE_DIR}/cmake/CommonLibSSE.cmake")
    message(FATAL_ERROR "extern/CommonLibSSE-NG is empty. Run: git submodule update --init --recursive --depth 1")
endif()
if(NOT EXISTS "${CLIB_SOURCE_DIR}/extern/openvr/lib/win64/openvr_api.lib")
    message(FATAL_ERROR "extern/CommonLibSSE-NG/extern/openvr is empty (VR is enabled). Run: git submodule update --init --recursive --depth 1")
endif()

# Every CACHE FORCE must precede add_subdirectory: option() inside NG cannot override an existing cache entry.
set(BUILD_TESTS OFF CACHE BOOL "" FORCE)                 # NG defaults ON -> find_package(Catch2 REQUIRED)
set(SKSE_SUPPORT_XBYAK OFF CACHE BOOL "" FORCE)          # the widget uses no trampoline
set(SKSE_SUPPORT_PATCH_SAFETY OFF CACHE BOOL "" FORCE)   # the widget has no hooks; ON downloads minhook at configure time
# One DLL for every runtime, dispatched at run time.
set(ENABLE_SKYRIM_SE ON CACHE BOOL "" FORCE)
set(ENABLE_SKYRIM_AE ON CACHE BOOL "" FORCE)             # AE = 1.6.x AND 1.7.x (NG classifies minor >= 6 as AE)
set(ENABLE_SKYRIM_VR ON CACHE BOOL "" FORCE)
set(REX_OPTION_INI OFF CACHE BOOL "" FORCE)
set(REX_OPTION_JSON OFF CACHE BOOL "" FORCE)
set(REX_OPTION_TOML OFF CACHE BOOL "" FORCE)

add_subdirectory("${CLIB_SOURCE_DIR}" CommonLibSSE-NG)
# NG's source build does not include its plugin helper (only the prebuilt path does).
include("${CLIB_SOURCE_DIR}/cmake/CommonLibSSE.cmake")

# MINIMUM_SKSE_VERSION must be a full x.y.z triplet (the helper's bare "0" default does not compile).
add_commonlibsse_plugin(${PROJECT_NAME} MINIMUM_SKSE_VERSION 0.0.0 SOURCES
    src/plugin.cpp
    src/CoLReader.cpp
    src/JsonStore.cpp
    src/Settings.cpp
    src/HudUI.cpp
    src/SettingsUI.cpp
    src/Visibility.cpp
    src/HotkeyHandler.cpp
    src/iHUDBridge.cpp
)
target_compile_features(${PROJECT_NAME} PRIVATE cxx_std_23)
target_precompile_headers(${PROJECT_NAME} PRIVATE PCH.h)
target_compile_definitions(${PROJECT_NAME} PRIVATE LILITHWIDGET_VERSION="${PROJECT_VERSION}")

target_include_directories(${PROJECT_NAME} PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/include
)

if(DEFINED OUTPUT_FOLDER)
    set(DLL_FOLDER "${OUTPUT_FOLDER}/SKSE/Plugins")
    message(STATUS "SKSE plugin output: ${DLL_FOLDER}")
    add_custom_command(
        TARGET "${PROJECT_NAME}" POST_BUILD
        COMMAND "${CMAKE_COMMAND}" -E make_directory "${DLL_FOLDER}"
        COMMAND "${CMAKE_COMMAND}" -E copy_if_different "$<TARGET_FILE:${PROJECT_NAME}>" "${DLL_FOLDER}/$<TARGET_FILE_NAME:${PROJECT_NAME}>"
        VERBATIM
    )
endif()
