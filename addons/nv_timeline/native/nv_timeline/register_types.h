#pragma once

#include <godot_cpp/godot.hpp>

// 插件的注册入口独立于项目已有 UI 扩展，避免插件类名和初始化生命周期互相污染。
void initialize_nv_timeline_types(godot::ModuleInitializationLevel p_level);
void uninitialize_nv_timeline_types(godot::ModuleInitializationLevel p_level);
