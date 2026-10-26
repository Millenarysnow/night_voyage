# Use the same minimum as native/rider-plugin/CMakeLists.txt
cmake_minimum_required(VERSION 3.17)

project(Project)

set(GODOTCPP_API_VERSION "4.8" CACHE STRING "Godot API version" FORCE)

# Optional
include("${CMAKE_CURRENT_LIST_DIR}/cmake/GodotCppSetup.cmake" OPTIONAL)
include("${CMAKE_CURRENT_LIST_DIR}/cmake/GodotAddonsManager.cmake" OPTIONAL)

# What would be shown in the SolutionView and Search
file(GLOB_RECURSE PROJECT_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/*")
list(FILTER PROJECT_SOURCES EXCLUDE REGEX "${CMAKE_CURRENT_SOURCE_DIR}/native/night_voyageExt/.*")
# Exclude third-party addons from indexing/search
foreach(_managed_addon IN LISTS GODOT_MANAGED_ADDONS)
    list(FILTER PROJECT_SOURCES EXCLUDE REGEX "${CMAKE_CURRENT_SOURCE_DIR}/addons/${_managed_addon}/.*")
endforeach()
list(FILTER PROJECT_SOURCES EXCLUDE REGEX "${CMAKE_BINARY_DIR}")
# Exclude files and folders, which start with a dot, like .gitignore, .git
list(FILTER PROJECT_SOURCES EXCLUDE REGEX "${CMAKE_CURRENT_SOURCE_DIR}/\\..*")

add_custom_target(Project SOURCES ${PROJECT_SOURCES})

if(EXISTS "${CMAKE_CURRENT_LIST_DIR}/native/night_voyageExt")
    add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/native/night_voyageExt SYSTEM)
endif()

# 时间轴插件复用上面已经生成的 godot::cpp 目标，但源码和产物保持在插件
# 自己的 addons/nv_timeline 目录内，便于将来单独复制到其他 Godot 项目。
if(EXISTS "${CMAKE_CURRENT_LIST_DIR}/addons/nv_timeline/native/nv_timeline")
    add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/addons/nv_timeline/native/nv_timeline SYSTEM)
endif()

# Prevent Godot from importing build artifacts as project resources
file(TOUCH "${CMAKE_BINARY_DIR}/.gdignore")
