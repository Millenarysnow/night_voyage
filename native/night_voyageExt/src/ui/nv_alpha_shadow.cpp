#include "nv_alpha_shadow.h"

#include <godot_cpp/classes/rendering_server.hpp>
#include <godot_cpp/classes/resource_loader.hpp>
#include <godot_cpp/classes/shader.hpp>
#include <godot_cpp/classes/viewport_texture.hpp>
#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/variant/callable.hpp>
#include <algorithm>
#include <cmath>

namespace godot {

NVAlphaShadow::NVAlphaShadow() {
	set_mouse_filter(MOUSE_FILTER_IGNORE);
	set_process(false);
}

NVAlphaShadow::~NVAlphaShadow() {
	const Callable texture_callback(this, "_texture_changed");
	if (texture.is_valid() && texture->is_connected("changed", texture_callback)) {
		texture->disconnect("changed", texture_callback);
	}
}

void NVAlphaShadow::_bind_methods() {
#define NV_BIND_ALPHA(type, name, hint, hint_text) \
	ClassDB::bind_method(D_METHOD("set_" #name, "value"), &NVAlphaShadow::set_##name); \
	ClassDB::bind_method(D_METHOD("get_" #name), &NVAlphaShadow::get_##name); \
	ADD_PROPERTY(PropertyInfo(type, #name, hint, hint_text), "set_" #name, "get_" #name);
	NV_BIND_ALPHA(Variant::OBJECT, texture, PROPERTY_HINT_RESOURCE_TYPE, "Texture2D")
	NV_BIND_ALPHA(Variant::COLOR, shadow_color, PROPERTY_HINT_NONE, "")
	NV_BIND_ALPHA(Variant::VECTOR2, shadow_offset, PROPERTY_HINT_NONE, "")
	NV_BIND_ALPHA(Variant::FLOAT, blur_radius, PROPERTY_HINT_RANGE, "0,96,0.1")
	NV_BIND_ALPHA(Variant::BOOL, show_source, PROPERTY_HINT_NONE, "")
	ADD_GROUP("Cache", "");
	NV_BIND_ALPHA(Variant::FLOAT, cache_scale, PROPERTY_HINT_RANGE, "0.125,1,0.125")
	NV_BIND_ALPHA(Variant::INT, max_cache_size, PROPERTY_HINT_RANGE, "64,2048,64")
#undef NV_BIND_ALPHA
	ClassDB::bind_method(D_METHOD("refresh_effect"), &NVAlphaShadow::refresh_effect);
	ClassDB::bind_method(D_METHOD("invalidate_cache"), &NVAlphaShadow::invalidate_cache);
	ClassDB::bind_method(D_METHOD("get_cache_build_count"), &NVAlphaShadow::get_cache_build_count);
	ClassDB::bind_method(D_METHOD("is_cache_pending"), &NVAlphaShadow::is_cache_pending);
	ClassDB::bind_method(D_METHOD("get_cache_size_pixels"), &NVAlphaShadow::get_cache_size_pixels);
	ClassDB::bind_method(D_METHOD("_texture_changed"), &NVAlphaShadow::on_texture_changed);
	ClassDB::bind_method(D_METHOD("_on_post_draw"), &NVAlphaShadow::on_post_draw);
	ADD_SIGNAL(MethodInfo("cache_ready"));
}

void NVAlphaShadow::_notification(int p_what) {
	if (p_what == NOTIFICATION_ENTER_TREE) {
		request_refresh(true);
	} else if (p_what == NOTIFICATION_EXIT_TREE) {
		stop_render_callback();
	} else if (p_what == NOTIFICATION_RESIZED) {
		request_refresh(true);
	}
}

void NVAlphaShadow::set_texture(const Ref<Texture2D> &p_texture) {
	if (texture == p_texture) { return; }
	const Callable callback(this, "_texture_changed");
	if (texture.is_valid() && texture->is_connected("changed", callback)) {
		texture->disconnect("changed", callback);
	}
	texture = p_texture;
	if (texture.is_valid()) { texture->connect("changed", callback); }
	request_refresh(true);
}

void NVAlphaShadow::on_texture_changed() { request_refresh(true); }
void NVAlphaShadow::invalidate_cache() { request_refresh(true); }

void NVAlphaShadow::set_shadow_color(Color p_value) {
	if (!std::isfinite(p_value.r) || !std::isfinite(p_value.g) ||
		!std::isfinite(p_value.b) || !std::isfinite(p_value.a)) { return; }
	if (shadow_color != p_value) { shadow_color = p_value; request_refresh(false); }
}

void NVAlphaShadow::set_shadow_offset(Vector2 p_value) {
	if (p_value.is_finite() && shadow_offset != p_value) { shadow_offset = p_value; request_refresh(false); }
}

void NVAlphaShadow::set_blur_radius(double p_value) {
	if (!std::isfinite(p_value)) { return; }
	p_value = std::clamp(p_value, 0.0, 96.0);
	if (blur_radius != p_value) { blur_radius = p_value; request_refresh(true); }
}

void NVAlphaShadow::set_cache_scale(double p_value) {
	if (!std::isfinite(p_value)) { return; }
	p_value = std::clamp(p_value, 0.125, 1.0);
	if (cache_scale != p_value) { cache_scale = p_value; request_refresh(true); }
}

void NVAlphaShadow::set_max_cache_size(int p_value) {
	p_value = std::clamp(p_value, 64, 2048);
	if (max_cache_size != p_value) { max_cache_size = p_value; request_refresh(true); }
}

void NVAlphaShadow::set_show_source(bool p_value) {
	if (show_source != p_value) { show_source = p_value; request_refresh(false); }
}

void NVAlphaShadow::request_refresh(bool p_rebuild) {
	dirty_flags |= p_rebuild ? 3 : 1;
	if (!is_inside_tree() || refresh_queued) { return; }
	refresh_queued = true;
	call_deferred("refresh_effect");
}

void NVAlphaShadow::ensure_pipeline() {
	if (display_material.is_valid()) { return; }
	display_material.instantiate();
	display_material->set_shader(ResourceLoader::get_singleton()->load(
		"res://addons/night_voyageExt/shaders/alpha_shadow.gdshader", "Shader"));
	set_material(display_material);
	Ref<Shader> blur_shader = ResourceLoader::get_singleton()->load(
		"res://addons/night_voyageExt/shaders/alpha_blur.gdshader", "Shader");
	horizontal_material.instantiate();
	horizontal_material->set_shader(blur_shader);
	vertical_material.instantiate();
	vertical_material->set_shader(blur_shader);

	auto create_stage = [this](const char *p_name, const Ref<ShaderMaterial> &p_material,
		SubViewport *&p_viewport, ColorRect *&p_quad) {
		p_viewport = memnew(SubViewport);
		p_viewport->set_name(p_name);
		p_viewport->set_disable_3d(true);
		p_viewport->set_transparent_background(true);
		p_viewport->set_disable_input(true);
		p_viewport->set_update_mode(SubViewport::UPDATE_DISABLED);
		p_viewport->set_clear_mode(SubViewport::CLEAR_MODE_ALWAYS);
		add_child(p_viewport, false, INTERNAL_MODE_BACK);
		// ColorRect 始终提交四边形；空 TextureRect 不绘制，无法执行模糊 Shader。
		p_quad = memnew(ColorRect);
		p_quad->set_color(Color(1, 1, 1, 1));
		p_quad->set_mouse_filter(MOUSE_FILTER_IGNORE);
		p_quad->set_material(p_material);
		p_viewport->add_child(p_quad, false, INTERNAL_MODE_BACK);
	};
	create_stage("HorizontalMaskCache", horizontal_material, horizontal_viewport, horizontal_quad);
	create_stage("VerticalMaskCache", vertical_material, vertical_viewport, vertical_quad);
}

void NVAlphaShadow::refresh_effect() {
	refresh_queued = false;
	if (!is_inside_tree()) { return; }
	ensure_pipeline();
	const int changes = dirty_flags;
	dirty_flags = 0;
	if (texture.is_null() || get_size().x <= 0.0 || get_size().y <= 0.0) {
		horizontal_viewport->set_update_mode(SubViewport::UPDATE_DISABLED);
		vertical_viewport->set_update_mode(SubViewport::UPDATE_DISABLED);
		render_stage = 0;
		stop_render_callback();
		queue_redraw();
		return;
	}
	if (changes & 2) { rebuild_cache(); }
	update_display();
	queue_redraw();
}

void NVAlphaShadow::rebuild_cache() {
	cache_padding = std::ceil(blur_radius * 3.0 + 3.0);
	logical_cache_size = get_size() + Vector2(cache_padding * 2.0, cache_padding * 2.0);
	const double longest = std::max(double(logical_cache_size.x), double(logical_cache_size.y));
	// 将缓存中的 σ 限制到 4 像素，25 个采样点覆盖 ±3σ 时相邻点不超过一像素。
	// 否则大半径配稀疏采样会出现条带；大模糊本来就没有高频细节，可安全降低缓存分辨率。
	// cache_scale 是分辨率上限；极细 Alpha 轮廓在低分辨率下仍可能损失，应按实际素材验收。
	const double kernel_scale = 4.0 / std::max(blur_radius, 4.0);
	const double scale = std::min({ cache_scale, kernel_scale, double(max_cache_size) / longest });
	const Vector2i cache_pixels(std::max(2, int(std::ceil(logical_cache_size.x * scale))),
		std::max(2, int(std::ceil(logical_cache_size.y * scale))));
	horizontal_viewport->set_size(cache_pixels);
	vertical_viewport->set_size(cache_pixels);
	horizontal_quad->set_size(Vector2(cache_pixels));
	vertical_quad->set_size(Vector2(cache_pixels));
	// Shader 用逻辑坐标计算，因此低分辨率缓存不改变阴影的视觉尺寸。
	for (const Ref<ShaderMaterial> &mat : { horizontal_material, vertical_material }) {
		mat->set_shader_parameter("canvas_size", logical_cache_size);
		mat->set_shader_parameter("content_origin", Vector2(cache_padding, cache_padding));
		mat->set_shader_parameter("content_size", get_size());
		mat->set_shader_parameter("sigma", blur_radius);
	}
	horizontal_material->set_shader_parameter("source_mask", texture);
	horizontal_material->set_shader_parameter("source_is_image", true);
	horizontal_material->set_shader_parameter("axis", Vector2(1, 0));
	vertical_material->set_shader_parameter("source_mask", horizontal_viewport->get_texture());
	vertical_material->set_shader_parameter("source_is_image", false);
	vertical_material->set_shader_parameter("axis", Vector2(0, 1));
	display_material->set_shader_parameter("blurred_mask", vertical_viewport->get_texture());
	// 横/纵两步分帧执行，显式保证纹理依赖，避免在不同渲染后端读到上一帧缓存。
	vertical_viewport->set_update_mode(SubViewport::UPDATE_DISABLED);
	horizontal_viewport->set_update_mode(SubViewport::UPDATE_ONCE);
	render_stage = 1;
	const Callable callback(this, "_on_post_draw");
	if (!RenderingServer::get_singleton()->is_connected("frame_post_draw", callback)) {
		RenderingServer::get_singleton()->connect("frame_post_draw", callback);
	}
	++cache_build_count;
}

void NVAlphaShadow::update_display() {
	draw_padding = cache_padding + std::max(std::abs(double(shadow_offset.x)), std::abs(double(shadow_offset.y))) + 2.0;
	display_material->set_shader_parameter("source_image", texture);
	display_material->set_shader_parameter("content_size", get_size());
	display_material->set_shader_parameter("cache_size", logical_cache_size);
	display_material->set_shader_parameter("cache_padding", cache_padding);
	display_material->set_shader_parameter("draw_padding", draw_padding);
	display_material->set_shader_parameter("shadow_offset", shadow_offset);
	display_material->set_shader_parameter("shadow_color", shadow_color);
	display_material->set_shader_parameter("show_source", show_source);
}

void NVAlphaShadow::on_post_draw() {
	if (render_stage == 1) {
		vertical_viewport->set_update_mode(SubViewport::UPDATE_ONCE);
		render_stage = 2;
	} else if (render_stage == 2) {
		render_stage = 0;
		// 稳定状态完全取消回调，许多静态图标也不产生每帧 C++ 调用。
		stop_render_callback();
		emit_signal("cache_ready");
	}
}

void NVAlphaShadow::stop_render_callback() {
	const Callable callback(this, "_on_post_draw");
	if (RenderingServer::get_singleton()->is_connected("frame_post_draw", callback)) {
		RenderingServer::get_singleton()->disconnect("frame_post_draw", callback);
	}
}

void NVAlphaShadow::_draw() {
	if (display_material.is_valid() && texture.is_valid() && get_size().x > 0 && get_size().y > 0) {
		draw_rect(Rect2(Vector2(-draw_padding, -draw_padding),
			get_size() + Vector2(draw_padding * 2.0, draw_padding * 2.0)), Color(1, 1, 1, 1));
	}
}

} // namespace godot
