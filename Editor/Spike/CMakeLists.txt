# Viewport spike (ADR-0004). Windows only. STATUS: UNVERIFIED — never configured
# or built by its author. Enabled with -DKENGINE_BUILD_EDITOR_SPIKE=ON.
if(NOT WIN32)
  message(FATAL_ERROR "Editor/Spike uses Direct3D 11 and builds on Windows only.")
endif()

# D3D compiler import library name differs between MSVC and MinGW-w64.
if(MSVC)
  set(KENGINE_D3DCOMPILER_LIB d3dcompiler)
else()
  set(KENGINE_D3DCOMPILER_LIB d3dcompiler_47)
endif()

# 1) GPU probe: no Qt needed. Build and run this FIRST.
add_executable(k_gpu_probe gpu_probe.cpp)
target_include_directories(k_gpu_probe PRIVATE "${CMAKE_SOURCE_DIR}")
target_link_libraries(k_gpu_probe PRIVATE d3d11 dxgi)

# 2) Viewport: needs Qt 6 Widgets (set CMAKE_PREFIX_PATH to your Qt kit).
find_package(Qt6 COMPONENTS Widgets)
if(Qt6_FOUND)
  add_executable(k_viewport_spike main.cpp ViewportWidget.cpp D3D11Renderer.cpp)
  set_target_properties(k_viewport_spike PROPERTIES AUTOMOC OFF AUTOUIC OFF AUTORCC OFF)
  target_include_directories(k_viewport_spike PRIVATE "${CMAKE_SOURCE_DIR}")
  target_link_libraries(k_viewport_spike PRIVATE kengine_core Qt6::Widgets d3d11 dxgi ${KENGINE_D3DCOMPILER_LIB})

  # Visual probe: runs the exe, decodes the PNG it writes and fails if surfaces are black.
  # Qt's DLLs must be findable (PATH or windeployqt). Needs a GPU or WARP.
  find_package(Python3 COMPONENTS Interpreter)
  if(Python3_Interpreter_FOUND)
    add_test(NAME viewport_screenshot
             COMMAND ${CMAKE_COMMAND} -E env "KENGINE_VIEWPORT_SPIKE=$<TARGET_FILE:k_viewport_spike>"
                     ${Python3_EXECUTABLE} -m unittest discover -s "${CMAKE_SOURCE_DIR}/Tests/Visual" -p test_viewport_screenshot.py -v)
    set_tests_properties(viewport_screenshot PROPERTIES TIMEOUT 120)
  endif()
else()
  message(WARNING "Qt6 Widgets not found: only k_gpu_probe will be built. Set CMAKE_PREFIX_PATH to your Qt 6 kit.")
endif()
