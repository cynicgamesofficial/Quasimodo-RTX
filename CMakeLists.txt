cmake_minimum_required (VERSION 3.15)
cmake_policy(SET CMP0069 NEW)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")

project(quake2-RTX)
set(Q2RTX_VERSION_MAJOR 1)
set(Q2RTX_VERSION_MINOR 8)
set(Q2RTX_VERSION_POINT 1)
set(STREAMLINE_ROOT "${PROJECT_SOURCE_DIR}/Third Parties/NVIDIA" CACHE PATH "Streamline SDK root")
set(NRD_ROOT "" CACHE PATH "NRD SDK source root")
get_filename_component(DEFAULT_LUNASVG_ROOT "${PROJECT_SOURCE_DIR}/Third Parties/lunasvg" ABSOLUTE)
set(Q2RTX_LUNASVG_ROOT "${DEFAULT_LUNASVG_ROOT}" CACHE PATH "Optional LunaSVG source root for RmlUi SVG plugin")
get_filename_component(DEFAULT_RMLUI_ROOT "${PROJECT_SOURCE_DIR}/Third Parties/RmlUi" ABSOLUTE)
set(RMLUI_ROOT "${DEFAULT_RMLUI_ROOT}" CACHE PATH "RmlUi source root")
set(Q2RTX_RMLUI_FONT_ENGINE "freetype" CACHE STRING "RmlUi font engine to use: freetype or none")
get_filename_component(DEFAULT_FREETYPE_ROOT "${PROJECT_SOURCE_DIR}/Third Parties/freetype" ABSOLUTE)
set(Q2RTX_FREETYPE_ROOT "${DEFAULT_FREETYPE_ROOT}" CACHE PATH "Optional FreeType package/install prefix for RmlUi")
set_property(CACHE Q2RTX_RMLUI_FONT_ENGINE PROPERTY STRINGS freetype none)

# get short-hash
execute_process(
	COMMAND git rev-parse --short HEAD
	WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
	OUTPUT_VARIABLE Q2RTX_VERSION_SHA
	OUTPUT_STRIP_TRAILING_WHITESPACE
)

# get branch name
execute_process(
    COMMAND git rev-parse --abbrev-ref HEAD
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    OUTPUT_VARIABLE Q2RTX_VERSION_BRANCH
    OUTPUT_STRIP_TRAILING_WHITESPACE
)


OPTION(CONFIG_GL_RENDERER "Enable GL renderer" ON)
OPTION(CONFIG_VKPT_RENDERER "Enable VKPT renderer" ON)
OPTION(CONFIG_VKPT_ENABLE_DEVICE_GROUPS "Enable device groups (multi-gpu) support" ON)
OPTION(CONFIG_VKPT_ENABLE_IMAGE_DUMPS "Enable image dumping functionality" OFF)
OPTION(CONFIG_USE_CURL "Use CURL for HTTP support" ON)
OPTION(CONFIG_LINUX_PACKAGING_SUPPORT "Enable Linux Packaging support" OFF)
OPTION(CONFIG_LINUX_PACKAGING_SKIP_PKZ "Skip zipping the game contents into .pkz when packaging (for quicker iteration)" OFF)
OPTION(CONFIG_LINUX_STEAM_RUNTIME_SUPPORT "Enable Linux Steam Runtime support" OFF)
IF(WIN32)
    SET(DEFAULT_BUILD_GLSLANG OFF)
ELSE()
    SET(DEFAULT_BUILD_GLSLANG ON)
ENDIF()
OPTION(CONFIG_BUILD_GLSLANG "Build glslangValidator from source instead of using the SDK" ${DEFAULT_BUILD_GLSLANG})
OPTION(QUASIMODO_TERRAIN "Enable Quasimodo terrain subsystem (.jungle)" ON)
OPTION(QUASIMODO_JOLT_PHYSICS "Enable optional Jolt physics integration (requires Third Parties/JoltPhysics)" OFF)
OPTION(CONFIG_BUILD_IPO "Enable interprocedural optimizations" OFF)
OPTION(CONFIG_BUILD_SHADER_DEBUG_INFO "Build shaders with debug info" OFF)
OPTION(USE_SYSTEM_ZLIB "Prefer system ZLIB instead of the bundled one" OFF)
OPTION(USE_SYSTEM_OPENAL "Prefer system OpenAL Soft instead of the bundled one" OFF)
OPTION(USE_SYSTEM_CURL "Prefer system cURL instead of the bundled one" OFF)
OPTION(USE_SYSTEM_SDL2 "Prefer system SDL2 instead of the bundled one" OFF)
set_property(GLOBAL PROPERTY USE_FOLDERS ON)

# use dynamic zlib for steam runtime
IF(CONFIG_LINUX_STEAM_RUNTIME_SUPPORT)
  SET(USE_SYSTEM_ZLIB ON)
ENDIF()

include(CheckIPOSupported)

IF(CONFIG_BUILD_IPO)
    check_ipo_supported(RESULT result OUTPUT output)
    if(result)
        MESSAGE(STATUS "Enabled interprocedural optimizations")
    else()
        MESSAGE(STATUS "Interprocedural optimizations not supported: ${output}")
        SET(CONFIG_BUILD_IPO OFF)
    endif()
ENDIF()

# ---------- Setup output Directories -------------------------
set (CMAKE_LIBRARY_OUTPUT_DIRECTORY
   ${PROJECT_BINARY_DIR}/Bin
   CACHE PATH
   "Single Directory for all Libraries"
   )

# --------- Setup the Executable output Directory -------------
set (CMAKE_RUNTIME_OUTPUT_DIRECTORY ${PROJECT_BINARY_DIR}/Bin CACHE PATH "Single Directory for all Executables.")
   
# --------- Dependencies -------------------------------------
if (CMAKE_SIZEOF_VOID_P EQUAL 8)
    set( IS_64_BIT 1 )
endif ()

set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

IF(MSVC)
    # Enable multi-processor compilation when using MSBuild
    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} /MP")
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} /MP")

ELSEIF(CMAKE_SYSTEM_PROCESSOR STREQUAL "ppc64le")

    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -mcpu=native")

ELSEIF(CMAKE_SYSTEM_PROCESSOR STREQUAL "e2k")

    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -mtune=native")

ELSEIF(CMAKE_SYSTEM_PROCESSOR STREQUAL "AMD64" OR CMAKE_SYSTEM_PROCESSOR STREQUAL "x86_64")

    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -msse2 -mfpmath=sse")

ENDIF()

# Work around CMAKE_INSTALL_PREFIX being a relative directory when using -B
# on Windows, which in turn trips up 'file(RELATIVE_PATH ...)' used in SDL2
set(CMAKE_INSTALL_PREFIX ${CMAKE_CURRENT_BINARY_DIR}/extern/install)

add_subdirectory(extern)

# Optional JoltPhysics (official clone under Third Parties/JoltPhysics — gitignored, see download-thirdparties.ps1).
# Library only: Jolt's Build/CMakeLists skips samples/tests when not the top-level project.
if(QUASIMODO_JOLT_PHYSICS)
	if(CMAKE_VERSION VERSION_LESS "3.20")
		message(FATAL_ERROR "QUASIMODO_JOLT_PHYSICS requires CMake 3.20+ (JoltPhysics Build/CMakeLists.txt). Current: ${CMAKE_VERSION}")
	endif()
	set(JOLT_SOURCE_DIR "${CMAKE_SOURCE_DIR}/Third Parties/JoltPhysics")
	set(JOLT_BUILD_DIR "${JOLT_SOURCE_DIR}/Build")
	if(NOT EXISTS "${JOLT_BUILD_DIR}/CMakeLists.txt")
		message(FATAL_ERROR "QUASIMODO_JOLT_PHYSICS is ON but JoltPhysics is missing.\nRun: .\\download-thirdparties.ps1\nExpected: ${JOLT_BUILD_DIR}/CMakeLists.txt")
	endif()
	# Avoid /WX from Jolt when embedded in Q2RTX (warnings differ across toolchain/SDK).
	# Note: this sets a CACHE entry; toggling QUASIMODO_JOLT_PHYSICS back OFF leaves the cache value
	# until cleared in CMake GUI or a fresh build directory.
	set(ENABLE_ALL_WARNINGS OFF CACHE BOOL "Jolt embedded in Q2RTX: disable warnings-as-errors" FORCE)
	add_subdirectory("${JOLT_BUILD_DIR}" "${CMAKE_BINARY_DIR}/extern/jolt")
	if(TARGET Jolt)
		set_target_properties(Jolt PROPERTIES FOLDER extern/Jolt)
	endif()
endif()

set(Q2RTX_FREETYPE_ADDED_FROM_SOURCE OFF)
if(IS_64_BIT AND Q2RTX_RMLUI_FONT_ENGINE STREQUAL "freetype" AND Q2RTX_FREETYPE_ROOT)
    if(EXISTS "${Q2RTX_FREETYPE_ROOT}/CMakeLists.txt" AND EXISTS "${Q2RTX_FREETYPE_ROOT}/include/ft2build.h")
        unset(Freetype_DIR CACHE)
        unset(FREETYPE_INCLUDE_DIR_freetype2 CACHE)
        unset(FREETYPE_INCLUDE_DIR_ft2build CACHE)
        unset(FREETYPE_LIBRARY_DEBUG CACHE)
        unset(FREETYPE_LIBRARY_RELEASE CACHE)
        unset(FIND_PACKAGE_MESSAGE_DETAILS_Freetype CACHE)
        set(FT_DISABLE_ZLIB ON CACHE BOOL "" FORCE)
        set(FT_DISABLE_BZIP2 ON CACHE BOOL "" FORCE)
        set(FT_DISABLE_PNG ON CACHE BOOL "" FORCE)
        set(FT_DISABLE_HARFBUZZ ON CACHE BOOL "" FORCE)
        set(FT_DISABLE_BROTLI ON CACHE BOOL "" FORCE)
        set(SKIP_INSTALL_ALL ON CACHE BOOL "" FORCE)
        add_subdirectory("${Q2RTX_FREETYPE_ROOT}" "${CMAKE_BINARY_DIR}/extern/freetype")
        if(TARGET freetype-interface AND NOT TARGET Freetype::Freetype)
            add_library(Freetype::Freetype ALIAS freetype-interface)
        elseif(TARGET freetype AND NOT TARGET Freetype::Freetype)
            add_library(Freetype::Freetype ALIAS freetype)
        endif()
        set(Q2RTX_FREETYPE_ADDED_FROM_SOURCE ON)
        set(CMAKE_DISABLE_FIND_PACKAGE_Freetype ON CACHE BOOL "" FORCE)
        set_target_properties(freetype freetype-interface PROPERTIES FOLDER extern/freetype)
    else()
        set(CMAKE_DISABLE_FIND_PACKAGE_Freetype OFF CACHE BOOL "" FORCE)
        list(APPEND CMAKE_PREFIX_PATH "${Q2RTX_FREETYPE_ROOT}")
    endif()
elseif(IS_64_BIT)
    set(CMAKE_DISABLE_FIND_PACKAGE_Freetype OFF CACHE BOOL "" FORCE)
endif()

set(Q2RTX_LUNASVG_ADDED_FROM_SOURCE OFF)
if(IS_64_BIT AND Q2RTX_LUNASVG_ROOT)
    if(EXISTS "${Q2RTX_LUNASVG_ROOT}/CMakeLists.txt" AND EXISTS "${Q2RTX_LUNASVG_ROOT}/include/lunasvg.h")
        unset(lunasvg_DIR CACHE)
        unset(LUNASVG_INCLUDE_DIR CACHE)
        unset(LUNASVG_LIBRARY_DEBUG CACHE)
        unset(LUNASVG_LIBRARY_RELEASE CACHE)
        unset(FIND_PACKAGE_MESSAGE_DETAILS_lunasvg CACHE)
        set(LUNASVG_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
        set(PLUTOVG_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
        add_subdirectory("${Q2RTX_LUNASVG_ROOT}" "${CMAKE_BINARY_DIR}/extern/lunasvg")
        set(Q2RTX_LUNASVG_ADDED_FROM_SOURCE ON)
        set_target_properties(lunasvg plutovg PROPERTIES FOLDER extern/lunasvg)
    else()
        list(APPEND CMAKE_PREFIX_PATH "${Q2RTX_LUNASVG_ROOT}")
    endif()
endif()

if(IS_64_BIT AND RMLUI_ROOT AND EXISTS "${RMLUI_ROOT}/CMakeLists.txt")
    set(RMLUI_SAMPLES OFF CACHE BOOL "" FORCE)
    set(RMLUI_LUA_BINDINGS OFF CACHE BOOL "" FORCE)
    set(RMLUI_LOTTIE_PLUGIN OFF CACHE BOOL "" FORCE)
    set(RMLUI_SVG_PLUGIN ${Q2RTX_LUNASVG_ADDED_FROM_SOURCE} CACHE BOOL "" FORCE)
    set(RMLUI_FONT_ENGINE "${Q2RTX_RMLUI_FONT_ENGINE}" CACHE STRING "" FORCE)
    set(RMLUI_PRECOMPILED_HEADERS OFF CACHE BOOL "" FORCE)
    set(RMLUI_WARNINGS_AS_ERRORS OFF CACHE BOOL "" FORCE)
    set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
    add_subdirectory("${RMLUI_ROOT}" "${CMAKE_BINARY_DIR}/extern/RmlUi")
    set_target_properties(rmlui_core rmlui_debugger PROPERTIES FOLDER extern/RmlUi)
    if(Q2RTX_LUNASVG_ADDED_FROM_SOURCE)
        message(STATUS "RmlUi: SVG plugin enabled using LunaSVG from ${Q2RTX_LUNASVG_ROOT}")
    else()
        message(STATUS "RmlUi: SVG plugin disabled (Q2RTX_LUNASVG_ROOT not configured)")
    endif()
else()
    message(STATUS "RmlUi: RMLUI_ROOT not set or CMakeLists.txt missing - RmlUi UI disabled at compile time")
endif()

link_directories(.)

set(CMAKE_INSTALL_PREFIX ${PROJECT_BINARY_DIR}/install)

SET(CMAKE_INTERPROCEDURAL_OPTIMIZATION ${CONFIG_BUILD_IPO})

add_subdirectory(src)

IF(CONFIG_LINUX_PACKAGING_SUPPORT)
    # https://cmake.org/cmake/help/v3.8/module/CPackDeb.html
    set(CPACK_GENERATOR "DEB")
    set(CPACK_PACKAGE_NAME "quake2rtx")
    set(CPACK_PACKAGE_VENDOR "NVIDIA Corporation")
    set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "RTX Remaster of Quake 2")
    set(CPACK_RESOURCE_FILE_LICENSE "${CMAKE_SOURCE_DIR}/license.txt")
    set(CPACK_PACKAGE_VERSION_MAJOR ${Q2RTX_VERSION_MAJOR})
    set(CPACK_PACKAGE_VERSION_MINOR ${Q2RTX_VERSION_MINOR})
    set(CPACK_PACKAGE_VERSION_PATCH ${Q2RTX_VERSION_POINT})
    set(CPACK_DEBIAN_PACKAGE_SECTION "games")
    
    IF(CMAKE_SYSTEM_PROCESSOR STREQUAL "aarch64")
        set(CPACK_DEBIAN_PACKAGE_ARCHITECTURE "arm64")
    ELSEIF(CMAKE_SYSTEM_PROCESSOR STREQUAL "ppc64le")
        set(CPACK_DEBIAN_PACKAGE_ARCHITECTURE "ppc64el")
    ELSEIF(CMAKE_SYSTEM_PROCESSOR STREQUAL "e2k")
        set(CPACK_DEBIAN_PACKAGE_ARCHITECTURE "e2k")
    ELSE()
        set(CPACK_DEBIAN_PACKAGE_ARCHITECTURE "amd64")
    ENDIF()

    set(CPACK_PACKAGE_FILE_NAME "${CPACK_PACKAGE_NAME}-${CPACK_PACKAGE_VERSION_MAJOR}.${CPACK_PACKAGE_VERSION_MINOR}.${CPACK_PACKAGE_VERSION_PATCH}-${CPACK_DEBIAN_PACKAGE_ARCHITECTURE}")

    set(CPACK_PACKAGE_CONTACT "http://nvidia.com/object/support.html")

    set(CPACK_DEBIAN_PACKAGE_HOMEPAGE "https://www.nvidia.com/en-us/geforce/news/quake-ii-rtx-ray-tracing-vulkan-vkray-geforce-rtx/")

    set(CPACK_DEBIAN_PACKAGE_DEPENDS "libvulkan1")

    IF(USE_SYSTEM_ZLIB)
      set(CPACK_DEBIAN_PACKAGE_DEPENDS "zlib1g")
    ENDIF()

    set(CPACK_DEB_PACKAGE_COMPONENT ON)
    set(CPACK_DEB_COMPONENT_INSTALL ON)
    set(CPACK_DEBIAN_ENABLE_COMPONENT_DEPENDS ON)

    # Mark as shareware to be clear this is demo levels + RTX
    set(CPACK_COMPONENTS_ALL shareware)

    include(CPack)
ENDIF()
