add_executable(mnemosyne
  mnemosyne_main.cc
  store.cc
  partition.cc
)
target_link_libraries(mnemosyne PRIVATE eidolon_common)
