add_executable(themis
  themis_main.cc
  scanner.cc
  rebalancer.cc
)
target_link_libraries(themis PRIVATE eidolon_common)
