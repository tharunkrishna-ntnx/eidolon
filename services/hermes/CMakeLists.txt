add_executable(hermes
  hermes_main.cc
  io_handler.cc
  replication.cc
)
target_link_libraries(hermes PRIVATE eidolon_common)
