# The following lines of boilerplate have to be in your project's
# CMakeLists in this exact order for cmake to work correctly
cmake_minimum_required(VERSION 3.16)

if(NOT DEFINED ENV{MMIOT_ROOT})
    message(FATAL_ERROR "MMIOT_ROOT environment variable not defined. Please set as the path to the framework directory in the MM-IoT-SDK")
endif()

message(STATUS "MMIOT_ROOT: $ENV{MMIOT_ROOT}")

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(porting_assistant)
