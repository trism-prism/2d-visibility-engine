add_library(viscore STATIC
  geometry.cpp
  grid.cpp
  scene.cpp
  visibility.cpp
  scenes.cpp)
target_include_directories(viscore PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(viscore PRIVATE vis_warnings)

# orient() relies on error-free transforms: no fast-math, no reassociation, no contraction.
# /fp:precise is MSVC's default; it is stated here so a changed global flag cannot break it.
if(MSVC)
  target_compile_options(viscore PRIVATE /fp:precise)
else()
  target_compile_options(viscore PRIVATE -ffp-contract=off -fno-fast-math)
endif()
