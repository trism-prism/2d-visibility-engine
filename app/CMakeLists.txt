# Third-party code is pinned by tag and built without our warning flags.
FetchContent_Declare(glfw
  GIT_REPOSITORY https://github.com/glfw/glfw.git
  GIT_TAG 3.4
  GIT_SHALLOW TRUE)
set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_DOCS OFF CACHE BOOL "" FORCE)
set(GLFW_INSTALL OFF CACHE BOOL "" FORCE)

# Dear ImGui ships no CMakeLists; SOURCE_SUBDIR keeps FetchContent from looking for one.
FetchContent_Declare(imgui
  GIT_REPOSITORY https://github.com/ocornut/imgui.git
  GIT_TAG v1.92.9
  GIT_SHALLOW TRUE
  SOURCE_SUBDIR no-cmake-here)
FetchContent_MakeAvailable(glfw imgui)

add_library(imgui_lib STATIC
  ${imgui_SOURCE_DIR}/imgui.cpp
  ${imgui_SOURCE_DIR}/imgui_draw.cpp
  ${imgui_SOURCE_DIR}/imgui_tables.cpp
  ${imgui_SOURCE_DIR}/imgui_widgets.cpp
  ${imgui_SOURCE_DIR}/backends/imgui_impl_glfw.cpp
  ${imgui_SOURCE_DIR}/backends/imgui_impl_opengl3.cpp)
target_include_directories(imgui_lib SYSTEM PUBLIC ${imgui_SOURCE_DIR} ${imgui_SOURCE_DIR}/backends)
target_include_directories(imgui_lib PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_definitions(imgui_lib PUBLIC "IMGUI_USER_CONFIG=\"imconfig_user.h\"")
target_link_libraries(imgui_lib PUBLIC glfw opengl32)

add_executable(vis_app
  main.cpp
  app.cpp
  ui.cpp
  widgets.cpp
  meter.cpp
  details.cpp
  tour.cpp
  legend.cpp
  render.cpp
  script.cpp
  bmp.cpp)
target_link_libraries(vis_app PRIVATE viscore vis_warnings imgui_lib)

# Scripted interaction test: replays tests/scripts/main.script through ImGui's input queue and
# fails on any `expect` that does not hold. Needs an OpenGL-capable machine (window is hidden).
if(VIS_BUILD_TESTS)
  add_test(NAME app_script
    COMMAND vis_app --size 1280x800 --script ${CMAKE_SOURCE_DIR}/tests/scripts/main.script
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})
  set_tests_properties(app_script PROPERTIES LABELS app TIMEOUT 900)

  # A script that cannot be loaded must shut down cleanly with exit code 2.
  add_test(NAME app_script_missing
    COMMAND ${CMAKE_COMMAND} -DAPP=$<TARGET_FILE:vis_app>
            -P ${CMAKE_SOURCE_DIR}/tests/scripts/expect_script_load_failure.cmake)
  set_tests_properties(app_script_missing PROPERTIES LABELS app TIMEOUT 120)
endif()
