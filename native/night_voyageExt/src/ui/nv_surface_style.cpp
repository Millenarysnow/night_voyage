#include "nv_surface_style.h"

#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/variant/callable.hpp>
#include <algorithm>
#include <cmath>

namespace godot {

void NVSurfaceStyle::_bind_methods() {
	// 资源保留英文稳定键名，便于 C++、场景和策划导出数据共用同一套接口。
#define NV_BIND_STYLE(type, name, hint, hint_text) \
	ClassDB::bind_method(D_METHOD("set_" #name, "value"), &NVSurfaceStyle::set_##name); \
	ClassDB::bind_method(D_METHOD("get_" #name), &NVSurfaceStyle::get_##name); \
	ADD_PROPERTY(PropertyInfo(type, #name, hint, hint_text), "set_" #name, "get_" #name);
	ADD_GROUP("Surface", "");
	ClassDB::bind_method(D_METHOD("set_fill_mode", "mode"), &NVSurfaceStyle::set_fill_mode);
	ClassDB::bind_method(D_METHOD("get_fill_mode"), &NVSurfaceStyle::get_fill_mode);
	ClassDB::bind_method(D_METHOD("set_fill_gradient", "gradient"), &NVSurfaceStyle::set_fill_gradient);
	ClassDB::bind_method(D_METHOD("get_fill_gradient"), &NVSurfaceStyle::get_fill_gradient);
	ClassDB::bind_method(D_METHOD("_on_gradient_changed"), &NVSurfaceStyle::on_gradient_changed);
	ClassDB::bind_method(D_METHOD("set_gradient_start", "value"), &NVSurfaceStyle::set_gradient_start);
	ClassDB::bind_method(D_METHOD("get_gradient_start"), &NVSurfaceStyle::get_gradient_start);
	ClassDB::bind_method(D_METHOD("set_gradient_end", "value"), &NVSurfaceStyle::set_gradient_end);
	ClassDB::bind_method(D_METHOD("get_gradient_end"), &NVSurfaceStyle::get_gradient_end);
	ClassDB::bind_method(D_METHOD("set_gradient_center", "value"), &NVSurfaceStyle::set_gradient_center);
	ClassDB::bind_method(D_METHOD("get_gradient_center"), &NVSurfaceStyle::get_gradient_center);
	ClassDB::bind_method(D_METHOD("set_gradient_radius", "value"), &NVSurfaceStyle::set_gradient_radius);
	ClassDB::bind_method(D_METHOD("get_gradient_radius"), &NVSurfaceStyle::get_gradient_radius);
	BIND_ENUM_CONSTANT(FILL_SOLID);
	BIND_ENUM_CONSTANT(FILL_LINEAR_GRADIENT);
	BIND_ENUM_CONSTANT(FILL_RADIAL_GRADIENT);
	ADD_PROPERTY(PropertyInfo(Variant::INT, "fill_mode", PROPERTY_HINT_ENUM, "Solid,Linear Gradient,Radial Gradient"),
		"set_fill_mode", "get_fill_mode");
	NV_BIND_STYLE(Variant::VECTOR4, corner_radii, PROPERTY_HINT_NONE, "")
	NV_BIND_STYLE(Variant::COLOR, fill_color, PROPERTY_HINT_NONE, "")
	ADD_PROPERTY(PropertyInfo(Variant::OBJECT, "fill_gradient", PROPERTY_HINT_RESOURCE_TYPE, "Gradient"),
		"set_fill_gradient", "get_fill_gradient");
	ADD_GROUP("Gradient Coordinates", "gradient_");
	ADD_PROPERTY(PropertyInfo(Variant::VECTOR2, "gradient_start", PROPERTY_HINT_NONE, "normalized"),
		"set_gradient_start", "get_gradient_start");
	ADD_PROPERTY(PropertyInfo(Variant::VECTOR2, "gradient_end", PROPERTY_HINT_NONE, "normalized"),
		"set_gradient_end", "get_gradient_end");
	ADD_PROPERTY(PropertyInfo(Variant::VECTOR2, "gradient_center", PROPERTY_HINT_NONE, "normalized"),
		"set_gradient_center", "get_gradient_center");
	ADD_PROPERTY(PropertyInfo(Variant::VECTOR2, "gradient_radius", PROPERTY_HINT_NONE, "normalized"),
		"set_gradient_radius", "get_gradient_radius");
	NV_BIND_STYLE(Variant::COLOR, border_color, PROPERTY_HINT_NONE, "")
	NV_BIND_STYLE(Variant::FLOAT, border_width, PROPERTY_HINT_RANGE, "0,32,0.1")
	ADD_GROUP("Outer Shadow", "shadow_");
	NV_BIND_STYLE(Variant::COLOR, shadow_color, PROPERTY_HINT_NONE, "")
	NV_BIND_STYLE(Variant::VECTOR2, shadow_offset, PROPERTY_HINT_NONE, "")
	NV_BIND_STYLE(Variant::FLOAT, shadow_softness, PROPERTY_HINT_RANGE, "0,96,0.1")
	NV_BIND_STYLE(Variant::FLOAT, shadow_spread, PROPERTY_HINT_RANGE, "-96,96,0.1")
	ADD_GROUP("Glow", "glow_");
	NV_BIND_STYLE(Variant::COLOR, glow_color, PROPERTY_HINT_NONE, "")
	NV_BIND_STYLE(Variant::FLOAT, glow_softness, PROPERTY_HINT_RANGE, "0,96,0.1")
	NV_BIND_STYLE(Variant::FLOAT, glow_spread, PROPERTY_HINT_RANGE, "-96,96,0.1")
	ADD_GROUP("Inner Shadow", "inner_shadow_");
	NV_BIND_STYLE(Variant::COLOR, inner_shadow_color, PROPERTY_HINT_NONE, "")
	NV_BIND_STYLE(Variant::VECTOR2, inner_shadow_offset, PROPERTY_HINT_NONE, "")
	NV_BIND_STYLE(Variant::FLOAT, inner_shadow_softness, PROPERTY_HINT_RANGE, "0,96,0.1")
#undef NV_BIND_STYLE
}

NVSurfaceStyle::~NVSurfaceStyle() {
	const Callable callback(this, "_on_gradient_changed");
	if (fill_gradient.is_valid() && fill_gradient->is_connected("changed", callback)) {
		fill_gradient->disconnect("changed", callback);
	}
}

void NVSurfaceStyle::set_fill_mode(FillMode p_mode) {
	if (p_mode < FILL_SOLID || p_mode > FILL_RADIAL_GRADIENT || fill_mode == p_mode) {
		return;
	}
	fill_mode = p_mode;
	emit_changed();
}

void NVSurfaceStyle::set_fill_gradient(const Ref<Gradient> &p_gradient) {
	if (fill_gradient == p_gradient) {
		return;
	}
	const Callable callback(this, "_on_gradient_changed");
	if (fill_gradient.is_valid() && fill_gradient->is_connected("changed", callback)) {
		fill_gradient->disconnect("changed", callback);
	}
	fill_gradient = p_gradient;
	if (fill_gradient.is_valid()) {
		fill_gradient->connect("changed", callback);
	}
	rebuild_gradient_texture();
	emit_changed();
}

void NVSurfaceStyle::on_gradient_changed() {
	// GradientTexture1D 持有同一个 Gradient，颜色停止点改变时会在 GPU 纹理内部更新。
	// 这里只通知使用该样式的控件重新上传资源，不重建纹理对象，避免编辑器拖动时反复分配。
	if (fill_gradient_texture.is_null() && fill_gradient.is_valid()) {
		rebuild_gradient_texture();
	}
	emit_changed();
}

void NVSurfaceStyle::rebuild_gradient_texture() {
	fill_gradient_texture.unref();
	if (fill_gradient.is_null()) {
		return;
	}
	fill_gradient_texture.instantiate();
	// 256 个采样点足以覆盖常规 UI 渐变；共享同一个 NVSurfaceStyle 的节点只保留一张小纹理。
	fill_gradient_texture->set_width(256);
	fill_gradient_texture->set_gradient(fill_gradient);
}

static Vector2 nv_normalized_coordinate(Vector2 p_value) {
	if (!p_value.is_finite()) {
		return Vector2();
	}
	return p_value.clamp(Vector2(-4, -4), Vector2(5, 5));
}

void NVSurfaceStyle::set_gradient_start(const Vector2 &p_value) {
	const Vector2 value = nv_normalized_coordinate(p_value);
	if (gradient_start == value) { return; }
	gradient_start = value;
	emit_changed();
}

void NVSurfaceStyle::set_gradient_end(const Vector2 &p_value) {
	const Vector2 value = nv_normalized_coordinate(p_value);
	if (gradient_end == value) { return; }
	gradient_end = value;
	emit_changed();
}

void NVSurfaceStyle::set_gradient_center(const Vector2 &p_value) {
	const Vector2 value = nv_normalized_coordinate(p_value);
	if (gradient_center == value) { return; }
	gradient_center = value;
	emit_changed();
}

void NVSurfaceStyle::set_gradient_radius(const Vector2 &p_value) {
	if (!p_value.is_finite()) { return; }
	const Vector2 value = p_value.abs().clamp(Vector2(0.001, 0.001), Vector2(5, 5));
	if (gradient_radius == value) { return; }
	gradient_radius = value;
	emit_changed();
}

void NVSurfaceStyle::set_corner_radii(const Vector4 &p_value) {
	if (!p_value.is_finite()) {
		return;
	}
	const Vector4 value = p_value.clamp(Vector4(), Vector4(2048, 2048, 2048, 2048));
	if (corner_radii != value) {
		corner_radii = value;
		emit_changed();
	}
}

// 直接值类型只在变化时发信号，多个使用者共享 .tres 不会发生每帧材质复制。
// Color 在 godot-cpp 中没有 is_finite()，逐通道检查可阻止 NaN 污染 GPU 参数。
static bool nv_finite(Color p_value) {
	return std::isfinite(p_value.r) && std::isfinite(p_value.g) &&
		std::isfinite(p_value.b) && std::isfinite(p_value.a);
}
static bool nv_finite(Vector2 p_value) { return p_value.is_finite(); }

#define NV_VALUE_SETTER(type, name) \
	void NVSurfaceStyle::set_##name(type p_value) { \
		if (!nv_finite(p_value) || name == p_value) { return; } \
		name = p_value; \
		emit_changed(); \
	}
NV_VALUE_SETTER(Color, fill_color)
NV_VALUE_SETTER(Color, border_color)
NV_VALUE_SETTER(Color, shadow_color)
NV_VALUE_SETTER(Vector2, shadow_offset)
NV_VALUE_SETTER(Color, glow_color)
NV_VALUE_SETTER(Color, inner_shadow_color)
NV_VALUE_SETTER(Vector2, inner_shadow_offset)
#undef NV_VALUE_SETTER

#define NV_SCALAR_SETTER(name, lower, upper) \
	void NVSurfaceStyle::set_##name(double p_value) { \
		if (!std::isfinite(p_value)) { return; } \
		p_value = std::clamp(p_value, double(lower), double(upper)); \
		if (name == p_value) { return; } \
		name = p_value; \
		emit_changed(); \
	}
NV_SCALAR_SETTER(border_width, 0, 32)
NV_SCALAR_SETTER(shadow_softness, 0, 96)
NV_SCALAR_SETTER(shadow_spread, -96, 96)
NV_SCALAR_SETTER(glow_softness, 0, 96)
NV_SCALAR_SETTER(glow_spread, -96, 96)
NV_SCALAR_SETTER(inner_shadow_softness, 0, 96)
#undef NV_SCALAR_SETTER

double NVSurfaceStyle::get_draw_padding() const {
	double padding = 1.0;
	if (shadow_color.a > 0.0) {
		padding = std::max(padding, 3.0 * shadow_softness + std::max(shadow_spread, 0.0) +
			std::max(std::abs(double(shadow_offset.x)), std::abs(double(shadow_offset.y))) + 2.0);
	}
	if (glow_color.a > 0.0) {
		padding = std::max(padding, 3.0 * glow_softness + std::max(glow_spread, 0.0) + 2.0);
	}
	return std::ceil(padding);
}

} // namespace godot
