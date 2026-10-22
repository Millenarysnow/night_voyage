#pragma once

#include <godot_cpp/classes/resource.hpp>
#include <godot_cpp/variant/color.hpp>
#include <godot_cpp/variant/vector2.hpp>

namespace godot {

/**
 * 可共享的模拟显示效果参数。各层使用独立开关/强度，渲染时合成为一次 Shader 绘制。
 * 动态层直接使用 Shader TIME；停用 motion 后画面稳定，不需要脚本轮询。
 */
class NVAnalogProfile : public Resource {
	GDCLASS(NVAnalogProfile, Resource)

	double brightness = 1.0;
	double contrast = 1.0;
	double saturation = 1.0;
	double scanline_strength = 0.0;
	double scanline_period = 3.0;
	double grain_strength = 0.0;
	double grain_scale = 1.0;
	double grain_speed = 12.0;
	double vignette_strength = 0.0;
	Color reflection_color = Color(0.8, 0.9, 1.0, 0.0);
	Vector2 reflection_center = Vector2(0.22, 0.0);
	Vector2 reflection_radius = Vector2(0.62, 0.46);
	double rolling_strength = 0.0;
	double rolling_speed = 0.11;
	double rolling_width = 0.035;
	double flicker_strength = 0.0;
	double flicker_frequency = 2.0;
	bool motion_enabled = true;
	bool overlay_blend = false;

protected:
	static void _bind_methods();

public:
#define NV_ANALOG_ACCESSOR(type, name) \
	void set_##name(type p_value); \
	type get_##name() const { return name; }
	NV_ANALOG_ACCESSOR(double, brightness)
	NV_ANALOG_ACCESSOR(double, contrast)
	NV_ANALOG_ACCESSOR(double, saturation)
	NV_ANALOG_ACCESSOR(double, scanline_strength)
	NV_ANALOG_ACCESSOR(double, scanline_period)
	NV_ANALOG_ACCESSOR(double, grain_strength)
	NV_ANALOG_ACCESSOR(double, grain_scale)
	NV_ANALOG_ACCESSOR(double, grain_speed)
	NV_ANALOG_ACCESSOR(double, vignette_strength)
	NV_ANALOG_ACCESSOR(Color, reflection_color)
	NV_ANALOG_ACCESSOR(Vector2, reflection_center)
	NV_ANALOG_ACCESSOR(Vector2, reflection_radius)
	NV_ANALOG_ACCESSOR(double, rolling_strength)
	NV_ANALOG_ACCESSOR(double, rolling_speed)
	NV_ANALOG_ACCESSOR(double, rolling_width)
	NV_ANALOG_ACCESSOR(double, flicker_strength)
	NV_ANALOG_ACCESSOR(double, flicker_frequency)
	NV_ANALOG_ACCESSOR(bool, motion_enabled)
	NV_ANALOG_ACCESSOR(bool, overlay_blend)
#undef NV_ANALOG_ACCESSOR
};

} // namespace godot
