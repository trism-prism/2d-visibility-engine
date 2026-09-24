# doctest is a single header: fetch it without running its own CMakeLists (SOURCE_SUBDIR points
# at a directory that has none), so the project is independent of that file's minimum CMake version.
FetchContent_Declare(doctest
  GIT_REPOSITORY https://github.com/doctest/doctest.git
  GIT_TAG v2.4.12
  GIT_SHALLOW TRUE
  SOURCE_SUBDIR no-cmake-here)
FetchContent_MakeAvailable(doctest)

add_executable(vis_tests
  test_main.cpp
  test_geometry.cpp
  test_exact.cpp
  test_polygons.cpp
  test_oracle.cpp
  test_differential.cpp
  test_grid.cpp)
target_include_directories(vis_tests SYSTEM PRIVATE ${doctest_SOURCE_DIR})
target_include_directories(vis_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
# Divides random-trial counts in the slow debug / sanitizer presets (see tests/test_util.h).
set(VIS_TEST_SCALE 1 CACHE STRING "Divisor for iteration and sample counts in tests")
target_compile_definitions(vis_tests PRIVATE VIS_TEST_SCALE=${VIS_TEST_SCALE})
target_link_libraries(vis_tests PRIVATE viscore vis_warnings)
add_test(NAME vis_tests COMMAND vis_tests)
