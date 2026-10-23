#pragma once

#include <godot_cpp/classes/gradient.hpp>
#include <godot_cpp/classes/gradient_texture1_d.hpp>
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

public:
	// 渐变类型只改变填充采样方式，阴影、描边、光晕仍由同一个表面 Shader 合成。
	enum FillMode {
		FILL_SOLID = 0,
		FILL_LINEAR_GRADIENT = 1,
		FILL_RADIAL_GRADIENT = 2,
	};

private:
	FillMode fill_mode = FILL_SOLID;
	Vector4 corner_radii = Vector4(16, 16, 16, 16);
	Color fill_color = Color(0.07, 0.09, 0.14, 1.0);
	Ref<Gradient> fill_gradient;
	Ref<GradientTexture1D> fill_gradient_texture;
	Vector2 gradient_start = Vector2(0.0, 0.0);
	Vector2 gradient_end = Vector2(1.0, 1.0);
	Vector2 gradient_center = Vector2(0.5, 0.5);
	Vector2 gradient_radius = Vector2(0.5, 0.5);
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

	void on_gradient_changed();
	void rebuild_gradient_texture();

protected:
	static void _bind_methods();

public:
	~NVSurfaceStyle() override;
	void set_fill_mode(FillMode p_mode);
	FillMode get_fill_mode() const { return fill_mode; }
	void set_fill_gradient(const Ref<Gradient> &p_gradient);
	Ref<Gradient> get_fill_gradient() const { return fill_gradient; }
	Ref<Texture2D> get_fill_gradient_texture() const { return fill_gradient_texture; }
	bool has_fill_gradient() const { return fill_gradient_texture.is_valid(); }
	void set_gradient_start(const Vector2 &p_value);
	Vector2 get_gradient_start() const { return gradient_start; }
	void set_gradient_end(const Vector2 &p_value);
	Vector2 get_gradient_end() const { return gradient_end; }
	void set_gradient_center(const Vector2 &p_value);
	Vector2 get_gradient_center() const { return gradient_center; }
	void set_gradient_radius(const Vector2 &p_value);
	Vector2 get_gradient_radius() const { return gradient_radius; }

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

VARIANT_ENUM_CAST(godot::NVSurfaceStyle::FillMode);
