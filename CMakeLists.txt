cmake_minimum_required(VERSION 3.21)

project(DashPilotAI VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if (NOT DEFINED ENV{GEODE_SDK})
    message(FATAL_ERROR "GEODE_SDK is not set. Install the matching Geode SDK, then restart your terminal.")
endif()

add_subdirectory("$ENV{GEODE_SDK}" "${CMAKE_CURRENT_BINARY_DIR}/geode")

file(GLOB_RECURSE SOURCES CONFIGURE_DEPENDS src/*.cpp)
add_library(${PROJECT_NAME} SHARED ${SOURCES})
setup_geode_mod(${PROJECT_NAME})
