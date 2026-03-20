cmake_minimum_required(VERSION 3.13)
project(reactnativeperfetto)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(REACT_NATIVE_PERFETTO_SOURCES
  src/main/jni/reactnativeperfetto-jni.cpp
  ../cpp/ReactNativePerfettoTracer.cpp
)

if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/../cpp/third_party/perfetto/sdk/perfetto.cc")
  list(APPEND REACT_NATIVE_PERFETTO_SOURCES ../cpp/third_party/perfetto/sdk/perfetto.cc)
endif()

add_library(reactnativeperfetto SHARED ${REACT_NATIVE_PERFETTO_SOURCES})

target_include_directories(
  reactnativeperfetto
  PRIVATE
  ../cpp
)

find_library(android-lib android)
find_library(log-lib log)

target_link_libraries(
  reactnativeperfetto
  ${android-lib}
  ${log-lib}
)
