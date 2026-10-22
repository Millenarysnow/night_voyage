#include "nv_analog_profile.h"

#include <godot_cpp/core/class_db.hpp>
#include <algorithm>
#include <cmath>

namespace godot {

static bool nv_finite(Color p_value) {
	return std::isfinite(p_value.r) && std::isfinite(p_value.g) &&
		std::isfinite(p_value.b) && std::isfinite(p_value.a);
}

void NVAnalogProfile::_bind_methods() {
// 参数按效果类别分组，策划可在 Inspector 或共享 .tres 中逐项调节，不需要逐帧脚本。
#define NV_BIND_ANALOG(type, name, hint, hint_text) \
	ClassDB::bind_method(D_METHOD("set_" #name, "value"), &NVAnalogProfile::set_##name); \
	ClassDB::bind_method(D_METHOD("get_" #name), &NVAnalogProfile::get_##name); \
	ADD_PROPERTY(PropertyInfo(type, #name, hint, hint_text), "set_" #name, "get_" #name);
	ADD_GROUP("Color Grade", "");
	NV_BIND_ANALOG(Variant::FLOAT, brightness, PROPERTY_HINT_RANGE, "0,2,0.01")
	NV_BIND_ANALOG(Variant::FLOAT, contrast, PROPERTY_HINT_RANGE, "0,2,0.01")
	NV_BIND_ANALOG(Variant::FLOAT, saturation, PROPERTY_HINT_RANGE, "0,2,0.01")
	ADD_GROUP("Scanline", "scanline_");
	NV_BIND_ANALOG(Variant::FLOAT, scanline_strength, PROPERTY_HINT_RANGE, "0,1,0.01")
	NV_BIND_ANALOG(Variant::FLOAT, scanline_period, PROPERTY_HINT_RANGE, "1,16,0.25,suffix:px")
	ADD_GROUP("Grain", "grain_");
	NV_BIND_ANALOG(Variant::FLOAT, grain_strength, PROPERTY_HINT_RANGE, "0,0.5,0.005")
	NV_BIND_ANALOG(Variant::FLOAT, grain_scale, PROPERTY_HINT_RANGE, "0.1,4,0.05")
	NV_BIND_ANALOG(Variant::FLOAT, grain_speed, PROPERTY_HINT_RANGE, "0,30,0.1")
	ADD_GROUP("Lens Surface", "");
	NV_BIND_ANALOG(Variant::FLOAT, vignette_strength, PROPERTY_HINT_RANGE, "0,1,0.01")
	NV_BIND_ANALOG(Variant::COLOR, reflection_color, PROPERTY_HINT_NONE, "")
	NV_BIND_ANALOG(Variant::VECTOR2, reflection_center, PROPERTY_HINT_NONE, "")
	NV_BIND_ANALOG(Variant::VECTOR2, reflection_radius, PROPERTY_HINT_NONE, "")
	ADD_GROUP("Rolling Band", "rolling_");
	NV_BIND_ANALOG(Variant::FLOAT, rolling_strength, PROPERTY_HINT_RANGE, "0,1,0.01")
	NV_BIND_ANALOG(Variant::FLOAT, rolling_speed, PROPERTY_HINT_RANGE, "0,1,0.005")
	NV_BIND_ANALOG(Variant::FLOAT, rolling_width, PROPERTY_HINT_RANGE, "0.005,0.2,0.001")
	ADD_GROUP("Flicker", "flicker_");
	NV_BIND_ANALOG(Variant::FLOAT, flicker_strength, PROPERTY_HINT_RANGE, "0,0.25,0.005")
	NV_BIND_ANALOG(Variant::FLOAT, flicker_frequency, PROPERTY_HINT_RANGE, "0,20,0.1")
	ADD_GROUP("Playback", "");
	NV_BIND_ANALOG(Variant::BOOL, motion_enabled, PROPERTY_HINT_NONE, "")
	NV_BIND_ANALOG(Variant::BOOL, overlay_blend, PROPERTY_HINT_NONE, "")
#undef NV_BIND_ANALOG
}

#define NV_ANALOG_SCALAR(name, lower, upper) \
	void NVAnalogProfile::set_##name(double p_value) { \
		if (!std::isfinite(p_value)) { return; } \
		/* 资源在上传到 Shader 前限制到 Inspector 提示的范围，避免 NaN/极端值污染整层画面。 */ \
		p_value = std::clamp(p_value, double(lower), double(upper)); \
		if (name != p_value) { name = p_value; emit_changed(); } \
	}
NV_ANALOG_SCALAR(brightness, 0, 2)
NV_ANALOG_SCALAR(contrast, 0, 2)
NV_ANALOG_SCALAR(saturation, 0, 2)
NV_ANALOG_SCALAR(scanline_strength, 0, 1)
NV_ANALOG_SCALAR(scanline_period, 1, 16)
NV_ANALOG_SCALAR(grain_strength, 0, 0.5)
NV_ANALOG_SCALAR(grain_scale, 0.1, 4)
NV_ANALOG_SCALAR(grain_speed, 0, 30)
NV_ANALOG_SCALAR(vignette_strength, 0, 1)
NV_ANALOG_SCALAR(rolling_strength, 0, 1)
NV_ANALOG_SCALAR(rolling_speed, 0, 1)
NV_ANALOG_SCALAR(rolling_width, 0.005, 0.2)
NV_ANALOG_SCALAR(flicker_strength, 0, 0.25)
NV_ANALOG_SCALAR(flicker_frequency, 0, 20)
#undef NV_ANALOG_SCALAR

void NVAnalogProfile::set_reflection_color(Color p_value) {
	if (!nv_finite(p_value) || reflection_color == p_value) { return; }
	reflection_color = p_value;
	emit_changed();
}

void NVAnalogProfile::set_reflection_center(Vector2 p_value) {
	if (!p_value.is_finite() || reflection_center == p_value) { return; }
	reflection_center = p_value;
	emit_changed();
}

void NVAnalogProfile::set_reflection_radius(Vector2 p_value) {
	if (!p_value.is_finite()) { return; }
	p_value.x = std::clamp(double(p_value.x), 0.01, 2.0);
	p_value.y = std::clamp(double(p_value.y), 0.01, 2.0);
	if (reflection_radius == p_value) { return; }
	reflection_radius = p_value;
	emit_changed();
}

void NVAnalogProfile::set_motion_enabled(bool p_value) {
	// 静态监控可关闭时间变化，颗粒会固定，亮带和闪烁不会继续消耗动画计算。
	if (motion_enabled != p_value) { motion_enabled = p_value; emit_changed(); }
}

void NVAnalogProfile::set_overlay_blend(bool p_value) {
	if (overlay_blend != p_value) { overlay_blend = p_value; emit_changed(); }
}

} // namespace godot
