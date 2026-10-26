# 时间轴插件的独立原生扩展构建文件。
#
# 根项目已经先构建了 godot::cpp 目标；这里直接复用该目标，避免把同一份
# Godot C++ 绑定重复编译，也让插件源码始终留在 addons/nv_timeline 边界内。

cmake_minimum_required(VERSION 3.17)

set(NV_TIMELINE_LIBNAME "nv_timeline")

# 这些属性由 godot-cpp 目标提供。不能直接依赖兄弟扩展目录里的同名 CMake
# 变量，因为变量作用域不会自动从 native/night_voyageExt 子目录传入这里。
get_target_property(NV_GODOTCPP_SUFFIX godot::cpp GODOTCPP_SUFFIX)
get_target_property(NV_GODOTCPP_PLATFORM godot::cpp GODOTCPP_PLATFORM)

file(GLOB_RECURSE NV_TIMELINE_SOURCES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/*.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/*.h"
)

add_library(${NV_TIMELINE_LIBNAME} SHARED ${NV_TIMELINE_SOURCES})
target_link_libraries(${NV_TIMELINE_LIBNAME} PRIVATE godot-cpp)
set_property(TARGET ${NV_TIMELINE_LIBNAME} PROPERTY CXX_STANDARD 17)
set_property(TARGET ${NV_TIMELINE_LIBNAME} PROPERTY CXX_STANDARD_REQUIRED ON)

# 源码包含中文注释和诊断文本；MSVC 必须明确使用 UTF-8，否则会在不同本机
# 代码页下产生乱码或把字符串字面量解析成错误的字节序列。
if(MSVC)
    target_compile_options(${NV_TIMELINE_LIBNAME} PRIVATE /utf-8)
else()
    target_compile_options(${NV_TIMELINE_LIBNAME} PRIVATE -Wall -Wextra -Wpedantic)
endif()

string(REPLACE ".universal" "" NV_TIMELINE_SUFFIX "${NV_GODOTCPP_SUFFIX}")
set_target_properties(${NV_TIMELINE_LIBNAME} PROPERTIES
    OUTPUT_NAME "${NV_TIMELINE_LIBNAME}${NV_TIMELINE_SUFFIX}"
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/../../bin/${NV_GODOTCPP_PLATFORM}"
)

if(WIN32)
    set_target_properties(${NV_TIMELINE_LIBNAME} PROPERTIES PREFIX "")
else()
    set_target_properties(${NV_TIMELINE_LIBNAME} PROPERTIES PREFIX "lib")
endif()

# 构建完成后同时把库复制到 native/bin，保留与已有扩展相同的本地调试目录
# 结构；真正被 Godot 加载的路径仍然是 addons/nv_timeline/bin。
set(NV_TIMELINE_NATIVE_BINARY_DIR
    "${CMAKE_CURRENT_SOURCE_DIR}/../bin/${NV_GODOTCPP_PLATFORM}"
)
add_custom_command(TARGET ${NV_TIMELINE_LIBNAME} POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory "${NV_TIMELINE_NATIVE_BINARY_DIR}"
    COMMAND ${CMAKE_COMMAND} -E copy
        "$<TARGET_FILE:${NV_TIMELINE_LIBNAME}>"
        "${NV_TIMELINE_NATIVE_BINARY_DIR}/$<TARGET_FILE_NAME:${NV_TIMELINE_LIBNAME}>"
)
