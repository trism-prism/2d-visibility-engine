# Headless benchmark: links viscore only, so it builds with VIS_BUILD_APP=OFF.
string(TOUPPER "${CMAKE_BUILD_TYPE}" _vis_bt)
set(VIS_BENCH_BUILD_TYPE "${CMAKE_BUILD_TYPE}")
set(VIS_BENCH_FLAGS "${CMAKE_CXX_FLAGS} ${CMAKE_CXX_FLAGS_${_vis_bt}}")
if(MSVC)
  string(APPEND VIS_BENCH_FLAGS " ; viscore adds /fp:precise")
endif()
if(VIS_SANITIZE)
  string(APPEND VIS_BENCH_FLAGS " ; /fsanitize=address")
endif()
string(STRIP "${VIS_BENCH_FLAGS}" VIS_BENCH_FLAGS)
configure_file(bench_config.h.in ${CMAKE_CURRENT_BINARY_DIR}/bench_config.h @ONLY)

add_executable(vis_bench main.cpp)
target_include_directories(vis_bench PRIVATE ${CMAKE_CURRENT_BINARY_DIR})
target_link_libraries(vis_bench PRIVATE viscore vis_warnings)

if(VIS_BUILD_TESTS)
  add_test(NAME bench_quick
    COMMAND vis_bench --out ${CMAKE_CURRENT_BINARY_DIR}/quick/results.csv --quick)
  set_tests_properties(bench_quick PROPERTIES LABELS bench TIMEOUT 300)
endif()
