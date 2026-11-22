# K Engine Editor (M0.5). Windows only. Enabled with -DKENGINE_BUILD_EDITOR=ON.
if(NOT WIN32)
  message(FATAL_ERROR "Editor/App uses Direct3D 11 and builds on Windows only.")
endif()

if(MSVC)
  set(KENGINE_D3DCOMPILER_LIB d3dcompiler)
else()
  set(KENGINE_D3DCOMPILER_LIB d3dcompiler_47)
endif()

find_package(Qt6 COMPONENTS Widgets)
if(Qt6_FOUND)
  # The Direct3D 11 renderer is shared with the spike: compile its source here instead of restructuring Editor/Spike.
  add_executable(k_editor
    main.cpp MainWindow.cpp EditorViewport.cpp
    "${CMAKE_SOURCE_DIR}/Editor/Spike/D3D11Renderer.cpp")
  set_target_properties(k_editor PROPERTIES AUTOMOC OFF AUTOUIC OFF AUTORCC OFF)
  target_include_directories(k_editor PRIVATE "${CMAKE_SOURCE_DIR}")
  target_link_libraries(k_editor PRIVATE kengine_editor_core kengine_core Qt6::Widgets d3d11 dxgi ${KENGINE_D3DCOMPILER_LIB})

  # Tests must not depend on the caller's PATH: put Qt's bin directory (<prefix>/lib/cmake/Qt6 -> <prefix>/bin)
  # in front of PATH for the test processes. (Missing DLLs show up as exit code 0xC0000135.)
  get_filename_component(KENGINE_QT_BIN_DIR "${Qt6_DIR}/../../../bin" ABSOLUTE)

  # Automated checks (need a GPU or WARP and a desktop session).
  add_test(NAME editor_smoke COMMAND k_editor --smoke)
  set_tests_properties(editor_smoke PROPERTIES TIMEOUT 120
                       ENVIRONMENT_MODIFICATION "PATH=path_list_prepend:${KENGINE_QT_BIN_DIR}")

  find_package(Python3 COMPONENTS Interpreter)
  if(Python3_Interpreter_FOUND)
    add_test(NAME editor_screenshot
             COMMAND ${CMAKE_COMMAND} -E env "KENGINE_VIEWPORT_SPIKE=$<TARGET_FILE:k_editor>" "KENGINE_VIEWPORT_ARGS=--demo"
                     ${Python3_EXECUTABLE} -m unittest discover -s "${CMAKE_SOURCE_DIR}/Tests/Visual" -p test_viewport_screenshot.py -v)
    set_tests_properties(editor_screenshot PROPERTIES TIMEOUT 120
                         ENVIRONMENT_MODIFICATION "PATH=path_list_prepend:${KENGINE_QT_BIN_DIR}")
  endif()
else()
  message(WARNING "Qt6 Widgets not found: k_editor will NOT be built. Set CMAKE_PREFIX_PATH to your Qt 6 kit.")
endif()
