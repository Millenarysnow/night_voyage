#include "nv_surface_style.h"

#include <godot_cpp/core/class_db.hpp>
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
	NV_BIND_STYLE(Variant::VECTOR4, corner_radii, PROPERTY_HINT_NONE, "")
	NV_BIND_STYLE(Variant::COLOR, fill_color, PROPERTY_HINT_NONE, "")
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
