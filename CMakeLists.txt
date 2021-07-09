cmake_minimum_required(VERSION 3.5)

project(LightingIO VERSION 0.2.1 LANGUAGES C CXX)

# for generate ycm complete database
SET(CMAKE_CXX_STANDARD 14)
SET(CMAKE_EXPORT_COMPILE_COMMANDS ON)

SET(CMAKE_CONFIG_DIR ${LightingIO_SOURCE_DIR}/cmake)
list(APPEND CMAKE_MODULE_PATH ${CMAKE_CONFIG_DIR}/Modules)

option(LTIO_BUILD_EXAMPLES "Build UNITTEST" ON)
option(LTIO_BUILD_UNITTESTS "Build UNITTEST" ON)
option(LTIO_BUILD_SHARED_LIBS "Build shared libraries" ON)
option(LTIO_WITH_OPENSSL "support ssl by openssl" OFF)

include(ExternalProject)
include(GNUInstallDirs)
include(${CMAKE_CONFIG_DIR}/common.cmake)
include(${CMAKE_CONFIG_DIR}/dependency.cmake)

include(${CMAKE_CONFIG_DIR}/include.cmake)
#include(${CMAKE_CONFIG_DIR}/googletest.cmake)
include(${CMAKE_CONFIG_DIR}/targets.cmake)

