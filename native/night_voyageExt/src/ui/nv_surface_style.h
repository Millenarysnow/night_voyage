#pragma once

#include <godot_cpp/classes/resource.hpp>
#include <godot_cpp/variant/color.hpp>
#include <godot_cpp/variant/vector2.hpp>
#include <godot_cpp/variant/vector4.hpp>

namespace godot {

/**
 * 可保存为 .tres 的 UI 表面样式，与具体剧情、窗口内容完全解耦。
 * 同一个资源可以供多个控件共用；属性变化通过 changed 信号一次性更新使用者。
 * 不使用 _process 轮询，也不把每个阴影层做成一个额外的 Viewport。
 */
class NVSurfaceStyle : public Resource {
	GDCLASS(NVSurfaceStyle, Resource)

	Vector4 corner_radii = Vector4(16, 16, 16, 16);
	Color fill_color = Color(0.07, 0.09, 0.14, 1.0);
	Color border_color = Color(0.50, 0.65, 0.85, 0.30);
	double border_width = 1.0;
	Color shadow_color = Color(0, 0, 0, 0.5);
	Vector2 shadow_offset = Vector2(0, 12);
	double shadow_softness = 12.0;
	double shadow_spread = 0.0;
	Color glow_color = Color(1.0, 0.65, 0.2, 0.0);
	double glow_softness = 12.0;
	double glow_spread = 0.0;
	Color inner_shadow_color = Color(0, 0, 0, 0);
	Vector2 inner_shadow_offset = Vector2(0, 3);
	double inner_shadow_softness = 5.0;

protected:
	static void _bind_methods();

public:
	// 左上、右上、右下、左下；四个半径独立设置，Shader 按实际控件大小限幅。
	void set_corner_radii(const Vector4 &p_value);
	Vector4 get_corner_radii() const { return corner_radii; }

#define NV_STYLE_ACCESSOR(type, name) \
	void set_##name(type p_value); \
	type get_##name() const { return name; }
	NV_STYLE_ACCESSOR(Color, fill_color)
	NV_STYLE_ACCESSOR(Color, border_color)
	NV_STYLE_ACCESSOR(double, border_width)
	NV_STYLE_ACCESSOR(Color, shadow_color)
	NV_STYLE_ACCESSOR(Vector2, shadow_offset)
	NV_STYLE_ACCESSOR(double, shadow_softness)
	NV_STYLE_ACCESSOR(double, shadow_spread)
	NV_STYLE_ACCESSOR(Color, glow_color)
	NV_STYLE_ACCESSOR(double, glow_softness)
	NV_STYLE_ACCESSOR(double, glow_spread)
	NV_STYLE_ACCESSOR(Color, inner_shadow_color)
	NV_STYLE_ACCESSOR(Vector2, inner_shadow_offset)
	NV_STYLE_ACCESSOR(double, inner_shadow_softness)
#undef NV_STYLE_ACCESSOR

	// 绘制向外扩张的上界：高斯保留 3σ，再加偏移和扩张，不裁掉投影。
	double get_draw_padding() const;
};

} // namespace godot
