add_executable(atlas
  atlas_main.cc
  vm_table.cc
  scheduler.cc
)
target_link_libraries(atlas PRIVATE eidolon_common)
