cmake_minimum_required (VERSION 3.8)

project ("MoonEngine")

find_package(Vulkan REQUIRED)
add_subdirectory(Libs)

set (CMAKE_RUNTIME_OUTPUT_DIRECTORY "${PROJECT_SOURCE_DIR}/bin")
set(CMAKE_CXX_STANDARD 20)

add_subdirectory(Moon)

## Shader Compilation
find_program(GLSL_VALIDATOR glslangValidator HINTS /usr/bin /usr/local/bin $ENV{VULKAN_SDK}/Bin/ $ENV{VULKAN_SDK}/Bin32/)

file(GLOB_RECURSE GLSL_SOURCE_FILES
    "${PROJECT_SOURCE_DIR}/Shaders/*.comp"     # Compute
    "${PROJECT_SOURCE_DIR}/Shaders/*.frag"     # Fragment
    "${PROJECT_SOURCE_DIR}/Shaders/*.geom"     # Geometry
    "${PROJECT_SOURCE_DIR}/Shaders/*.mesh"     # Mesh
    "${PROJECT_SOURCE_DIR}/Shaders/*.rahit"    # Ray any hit
    "${PROJECT_SOURCE_DIR}/Shaders/*.rcall"    # Ray callable
    "${PROJECT_SOURCE_DIR}/Shaders/*.rchit"    # Ray closest hit
    "${PROJECT_SOURCE_DIR}/Shaders/*.rgen"     # Ray generation
    "${PROJECT_SOURCE_DIR}/Shaders/*.rint"     # Ray intersection
    "${PROJECT_SOURCE_DIR}/Shaders/*.rmiss"    # Ray miss
    "${PROJECT_SOURCE_DIR}/Shaders/*.task"     # Task
    "${PROJECT_SOURCE_DIR}/Shaders/*.tesc"     # Tessellation control
    "${PROJECT_SOURCE_DIR}/Shaders/*.tese"     # Tessellation evaluation
    "${PROJECT_SOURCE_DIR}/Shaders/*.vert"     # Vertex
    )

foreach(GLSL ${GLSL_SOURCE_FILES})
  message(STATUS "BUILDING SHADER")
  get_filename_component(FILE_NAME ${GLSL} NAME)
  set(SPIRV "${PROJECT_SOURCE_DIR}/Shaders/${FILE_NAME}.spv")
  message(STATUS ${GLSL})
  add_custom_command(
    OUTPUT ${SPIRV}
    COMMAND ${GLSL_VALIDATOR} -V ${GLSL} -o ${SPIRV}
    DEPENDS ${GLSL})
  list(APPEND SPIRV_BINARY_FILES ${SPIRV})
endforeach(GLSL)

add_custom_target(
    Shaders 
    DEPENDS ${SPIRV_BINARY_FILES}
    )

