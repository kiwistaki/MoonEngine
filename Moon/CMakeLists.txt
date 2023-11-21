file(GLOB HEADER_FILES "${CMAKE_CURRENT_SOURCE_DIR}/*.h")
file(GLOB SOURCE_FILES "${CMAKE_CURRENT_SOURCE_DIR}/*.cpp")

add_executable (Moon  ${HEADER_FILES} ${SOURCE_FILES})

set_property(TARGET Moon PROPERTY VS_DEBUGGER_WORKING_DIRECTORY $<TARGET_FILE_DIR:Moon>)
target_include_directories(Moon PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")

target_link_libraries(Moon Vulkan::Vulkan sdl2)
target_link_libraries(Moon vkbootstrap vma glm tinyobjloader imgui stb_image)
