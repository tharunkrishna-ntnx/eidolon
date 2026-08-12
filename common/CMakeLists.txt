add_library(eidolon_common STATIC
  src/logging.cc
  src/gflags_util.cc
  src/net.cc
  src/service_base.cc
)

target_include_directories(eidolon_common PUBLIC
  ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(eidolon_common PUBLIC
  gflags::gflags
  glog::glog
  pthread
)
