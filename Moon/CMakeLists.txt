
add_executable (Moon 
  main.cpp
  RenderDevice.cpp
  RenderDevice.h
  RenderImage.cpp
  RenderImage.h
  RenderTypes.h
  RenderUtilities.cpp
  RenderUtilities.h)

set_property(TARGET Moon PROPERTY VS_DEBUGGER_WORKING_DIRECTORY $<TARGET_FILE_DIR:Moon>)
target_include_directories(Moon PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")

target_link_libraries(Moon Vulkan::Vulkan sdl2)
target_link_libraries(Moon vkbootstrap vma glm tinyobjloader imgui stb_image)
