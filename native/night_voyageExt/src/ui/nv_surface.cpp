#include "nv_surface.h"

#include <godot_cpp/classes/resource_loader.hpp>
#include <godot_cpp/classes/shader.hpp>
#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/variant/callable.hpp>

namespace godot {

NVSurface::NVSurface() {
	// 表面默认透传鼠标；真正的窗口或交互控件可自行改为 STOP。
	set_mouse_filter(MOUSE_FILTER_PASS);
	set_process(false);
}

NVSurface::~NVSurface() {
	const Callable callback(this, "_request_refresh");
	if (surface_style.is_valid() && surface_style->is_connected("changed", callback)) {
		surface_style->disconnect("changed", callback);
	}
}

void NVSurface::_bind_methods() {
	ClassDB::bind_method(D_METHOD("set_surface_style", "style"), &NVSurface::set_surface_style);
	ClassDB::bind_method(D_METHOD("get_surface_style"), &NVSurface::get_surface_style);
	ClassDB::bind_method(D_METHOD("refresh_effect"), &NVSurface::refresh_effect);
	ClassDB::bind_method(D_METHOD("_request_refresh"), &NVSurface::request_refresh);
	ADD_PROPERTY(PropertyInfo(Variant::OBJECT, "surface_style", PROPERTY_HINT_RESOURCE_TYPE, "NVSurfaceStyle"),
		"set_surface_style", "get_surface_style");
}

void NVSurface::set_surface_style(const Ref<NVSurfaceStyle> &p_style) {
	if (surface_style == p_style && surface_style.is_valid()) {
		return;
	}
	const Callable callback(this, "_request_refresh");
	if (surface_style.is_valid() && surface_style->is_connected("changed", callback)) {
		surface_style->disconnect("changed", callback);
	}
	surface_style = p_style;
	if (surface_style.is_null()) {
		surface_style = create_default_style();
	}
	surface_style->connect("changed", callback);
	request_refresh();
}

void NVSurface::_notification(int p_what) {
	if (p_what == NOTIFICATION_ENTER_TREE || p_what == NOTIFICATION_RESIZED) {
		// 延迟到进入场景再创建资源，避免把实例资源当作 ClassDB 属性默认值。
		if (is_inside_tree() && surface_style.is_null()) { set_surface_style(Ref<NVSurfaceStyle>()); }
		request_refresh();
	}
}

void NVSurface::request_refresh() {
	// 同一帧修改一组样式时合并成一次上传，编辑器拖动参数同样有效。
	if (!is_inside_tree() || refresh_queued) {
		return;
	}
	refresh_queued = true;
	call_deferred("refresh_effect");
}

void NVSurface::refresh_effect() {
	refresh_queued = false;
	if (is_inside_tree()) {
		update_effect();
		queue_redraw();
	}
}

String NVSurface::shader_path() const {
	return "res://addons/night_voyageExt/shaders/surface.gdshader";
}

Ref<NVSurfaceStyle> NVSurface::create_default_style() const {
	Ref<NVSurfaceStyle> style;
	style.instantiate();
	return style;
}

void NVSurface::update_effect() {
	if (effect_material.is_null()) {
		effect_material.instantiate();
		// ResourceLoader 缓存共享 Shader；每个控件只拥有自己的小型参数集。
		Ref<Shader> shader = ResourceLoader::get_singleton()->load(shader_path(), "Shader");
		effect_material->set_shader(shader);
		set_material(effect_material);
	}
	draw_padding = surface_style->get_draw_padding();
	effect_material->set_shader_parameter("surface_size", get_size());
	effect_material->set_shader_parameter("draw_padding", draw_padding);
	effect_material->set_shader_parameter("fill_gradient", surface_style->get_fill_gradient_texture());
	effect_material->set_shader_parameter("has_fill_gradient", surface_style->has_fill_gradient());
#define NV_UPLOAD(name) effect_material->set_shader_parameter(#name, surface_style->get_##name());
	NV_UPLOAD(fill_mode)
	NV_UPLOAD(corner_radii)
	NV_UPLOAD(fill_color)
	NV_UPLOAD(gradient_start)
	NV_UPLOAD(gradient_end)
	NV_UPLOAD(gradient_center)
	NV_UPLOAD(gradient_radius)
	NV_UPLOAD(border_color)
	NV_UPLOAD(border_width)
	NV_UPLOAD(shadow_color)
	NV_UPLOAD(shadow_offset)
	NV_UPLOAD(shadow_softness)
	NV_UPLOAD(shadow_spread)
	NV_UPLOAD(glow_color)
	NV_UPLOAD(glow_softness)
	NV_UPLOAD(glow_spread)
	NV_UPLOAD(inner_shadow_color)
	NV_UPLOAD(inner_shadow_offset)
	NV_UPLOAD(inner_shadow_softness)
#undef NV_UPLOAD
}

void NVSurface::_draw() {
	if (get_size().x > 0 && get_size().y > 0 && effect_material.is_valid()) {
		// 扩大绘制四边形但不扩大 Control 的布局/命中范围。
		draw_rect(Rect2(Vector2(-draw_padding, -draw_padding),
			get_size() + Vector2(draw_padding * 2.0, draw_padding * 2.0)), Color(1, 1, 1, 1));
	}
}

} // namespace godot
