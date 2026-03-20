cmake_minimum_required(VERSION 3.13)
project(appmodules)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

include(${REACT_ANDROID_DIR}/cmake-utils/ReactNative-application.cmake)

get_filename_component(REPO_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../../../../../../" ABSOLUTE)

find_package(react-native-perfetto REQUIRED CONFIG)

target_sources(
  ${CMAKE_PROJECT_NAME}
  PRIVATE
  # Keep this file non-.cpp so ReactNative-application.cmake still includes
  # the default OnLoad.cpp that wires core module providers (PlatformConstants, etc.).
  ${CMAKE_CURRENT_SOURCE_DIR}/apputilities-installer-jni.cc
  ${REPO_ROOT}/example-jsi/cpp/AppUtilitiesJSI.cpp
)

target_include_directories(
  ${CMAKE_PROJECT_NAME}
  PRIVATE
  ${REPO_ROOT}/example-jsi/cpp
)

find_library(android-lib android)
find_library(log-lib log)

target_link_libraries(
  ${CMAKE_PROJECT_NAME}
  react-native-perfetto::reactnativeperfetto
  ${android-lib}
  ${log-lib}
)
