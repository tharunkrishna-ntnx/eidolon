cmake_minimum_required(VERSION 3.16)
project(eidolon VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

option(EIDOLON_STATIC_LINK "Statically link third-party libraries" ON)

include(FetchContent)

# ---- gflags ----
FetchContent_Declare(
  gflags
  GIT_REPOSITORY https://github.com/gflags/gflags.git
  GIT_TAG        v2.2.2
)
set(GFLAGS_BUILD_STATIC_LIBS ON CACHE BOOL "" FORCE)
set(GFLAGS_BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
set(GFLAGS_BUILD_gflags_LIB ON CACHE BOOL "" FORCE)
set(GFLAGS_BUILD_TESTING OFF CACHE BOOL "" FORCE)
set(GFLAGS_INSTALL_HEADERS OFF CACHE BOOL "" FORCE)
set(INSTALL_HEADERS OFF CACHE BOOL "" FORCE)
set(GFLAGS_INSTALL_STATIC_LIBS OFF CACHE BOOL "" FORCE)
set(GFLAGS_INSTALL_SHARED_LIBS OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(gflags)

# ---- glog ----
FetchContent_Declare(
  glog
  GIT_REPOSITORY https://github.com/google/glog.git
  GIT_TAG        v0.6.0
)
set(WITH_GFLAGS ON CACHE BOOL "" FORCE)
set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
set(BUILD_TESTING OFF CACHE BOOL "" FORCE)
set(WITH_GTEST OFF CACHE BOOL "" FORCE)
set(WITH_UNWIND OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(glog)

add_subdirectory(common)
add_subdirectory(services/hermes)
add_subdirectory(services/mnemosyne)
add_subdirectory(services/themis)
