#ifndef REGISTER_TYPES_H
#define REGISTER_TYPES_H

#include <godot_cpp/godot.hpp>

// 声明与注册回调保持完全一致，便于在新增组件时由编译器检查签名。
void initialize_gdextension_types(godot::ModuleInitializationLevel p_level);
void uninitialize_gdextension_types(godot::ModuleInitializationLevel p_level);

#endif // REGISTER_TYPES_H
