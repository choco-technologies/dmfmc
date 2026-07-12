# =====================================================================
#               DMOD FMC Driver Module
# =====================================================================
cmake_minimum_required(VERSION 3.18)

# ======================================================================
#               Parameters
# ======================================================================
set(DMFMC_MCU_SERIES "stm32f7" CACHE STRING "Target MCU series")

# ======================================================================
#               Include target architecture configuration
# ======================================================================
include(${CMAKE_CURRENT_SOURCE_DIR}/src/port/${DMFMC_MCU_SERIES}/config.cmake)

# ======================================================================
#               For VS Code
# ======================================================================
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# ======================================================================
#               dmfmc Version
# ======================================================================
# Allow version to be passed as a parameter, default to 0.1
if(NOT DEFINED DMOD_MODULE_VERSION)
    set(DMOD_MODULE_VERSION "0.1" CACHE STRING "DMOD module version")
endif()

# ======================================================================
#               Fetch DMOD repository
# ======================================================================
include(FetchContent)
FetchContent_Declare(
    dmod
    GIT_REPOSITORY https://github.com/choco-technologies/dmod.git
    GIT_TAG        develop
)

# ======================================================================
#               DMOD Configuration
# ======================================================================
set(DMOD_MODE "DMOD_MODULE" CACHE STRING "DMOD build mode")
set(DMOD_BUILD_TESTS OFF CACHE BOOL "Build tests")
set(DMOD_BUILD_EXAMPLES OFF CACHE BOOL "Build examples")
set(DMOD_BUILD_TOOLS OFF CACHE BOOL "Build tools")
set(DMOD_BUILD_TEMPLATES OFF CACHE BOOL "Build templates")
set(DMOD_CPU_FAMILY ${DMFMC_MCU_SERIES} CACHE STRING "CPU Family Name")

FetchContent_MakeAvailable(dmod)

project(dmfmc
VERSION ${DMOD_MODULE_VERSION}
DESCRIPTION "DMOD FMC (Flexible Memory Controller) Driver Module"
LANGUAGES C CXX)

set(DMOD_DIR ${dmod_SOURCE_DIR} CACHE PATH "DMOD source directory")

# ======================================================================
#               Import dmod functions and macros
# ======================================================================
set(DMOD_DIR ${dmod_SOURCE_DIR} CACHE PATH "DMOD source directory")
set(DMOD_SCRIPTS_DIR ${DMOD_DIR}/scripts CACHE PATH "DMOD scripts directory")
include(${DMOD_DIR}/paths.cmake)
dmod_setup_external_module()

# ======================================================================
#               Subdirectories
# ======================================================================
add_subdirectory(src/port)

# ======================================================================
#               dmfmc Module Configuration
# ======================================================================
# Name of the module
set(DMOD_MODULE_NAME        dmfmc)

# Version is already set above and used in project()

# Author (should be string)
set(DMOD_AUTHOR_NAME        "Patryk Kubiak")

# Stack size for the module (should be integer)
set(DMOD_STACK_SIZE         1024)

# Path to the DMR file for this module
set(DMOD_DMR_PATH           ${CMAKE_CURRENT_SOURCE_DIR}/dmfmc.dmr)

#
#   dmod_add_library - create a library module
#   it has the same signature as add_library
#   and can be used in the same way after the creation
#   (for example, to link libraries)
#
dmod_add_library(${DMOD_MODULE_NAME} ${DMOD_MODULE_VERSION}
    # List of source files - can include C and C++ files
    src/dmfmc.c
    src/dmfmc_chips.c
)

dmod_link_modules(${DMOD_MODULE_NAME}
    dmdrvi
    dmini
    dmhaman
    dmheap@>=0.4
)

target_link_libraries(${DMOD_MODULE_NAME}
    dmfmc_port_if
)

target_include_directories(${DMOD_MODULE_NAME} PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)
