cmake_minimum_required(VERSION 3.16)
project(eidolon-misc VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

find_package(Threads REQUIRED)

function(add_workload name src)
  add_executable(${name} ${src})
  target_include_directories(${name} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/workloads)
  target_link_libraries(${name} PRIVATE Threads::Threads)
endfunction()

add_workload(task-worker   workloads/task_worker.cc)
add_workload(cache-daemon  workloads/cache_daemon.cc)
add_workload(index-builder workloads/index_builder.cc)
add_workload(health-probe  workloads/health_probe.cc)
add_workload(log-shipper   workloads/log_shipper.cc)
add_workload(page-scanner  workloads/page_scanner.cc)
add_workload(thread-pool   workloads/thread_pool.cc)
