#include "nv_screen_effect_viewport.h"

#include <godot_cpp/classes/input_event_gesture.hpp>
#include <godot_cpp/classes/input_event_mouse.hpp>
#include <godot_cpp/classes/input_event_mouse_motion.hpp>
#include <godot_cpp/classes/input_event_screen_drag.hpp>
#include <godot_cpp/classes/input_event_screen_touch.hpp>
#include <godot_cpp/classes/resource_loader.hpp>
#include <godot_cpp/classes/shader.hpp>
#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/variant/callable.hpp>
#include <algorithm>

namespace godot {

NVScreenEffectViewport::NVScreenEffectViewport() {
	set_notify_transform(true);
}

void NVScreenEffectViewport::_bind_methods() {
	ClassDB::bind_method(D_METHOD("set_warp_profile", "profile"), &NVScreenEffectViewport::set_warp_profile);
	ClassDB::bind_method(D_METHOD("get_warp_profile"), &NVScreenEffectViewport::get_warp_profile);
	ClassDB::bind_method(D_METHOD("set_analog_profile", "profile"), &NVScreenEffectViewport::set_analog_profile);
	ClassDB::bind_method(D_METHOD("get_analog_profile"), &NVScreenEffectViewport::get_analog_profile);
	ClassDB::bind_method(D_METHOD("refresh_effect"), &NVScreenEffectViewport::refresh_effect);
	ClassDB::bind_method(D_METHOD("_on_profile_changed"), &NVScreenEffectViewport::on_profile_changed);
	ADD_GROUP("Screen Warp", "");
	ADD_PROPERTY(PropertyInfo(Variant::OBJECT, "warp_profile", PROPERTY_HINT_RESOURCE_TYPE, "NVWarpProfile"),
		"set_warp_profile", "get_warp_profile");
	ADD_GROUP("Display Surface", "");
	ADD_PROPERTY(PropertyInfo(Variant::OBJECT, "analog_profile", PROPERTY_HINT_RESOURCE_TYPE, "NVAnalogProfile"),
		"set_analog_profile", "get_analog_profile");
}

void NVScreenEffectViewport::set_warp_profile(const Ref<NVWarpProfile> &p_profile) {
	if (warp_profile == p_profile) { return; }
	const Callable callback(this, "_on_profile_changed");
	if (warp_profile.is_valid() && warp_profile->is_connected("changed", callback)) { warp_profile->disconnect("changed", callback); }
	warp_profile = p_profile;
	if (warp_profile.is_valid()) { warp_profile->connect("changed", callback); }
	request_refresh();
}

void NVScreenEffectViewport::set_analog_profile(const Ref<NVAnalogProfile> &p_profile) {
	if (analog_profile == p_profile) { return; }
	const Callable callback(this, "_on_profile_changed");
	if (analog_profile.is_valid() && analog_profile->is_connected("changed", callback)) { analog_profile->disconnect("changed", callback); }
	analog_profile = p_profile;
	if (analog_profile.is_valid()) { analog_profile->connect("changed", callback); }
	request_refresh();
}

void NVScreenEffectViewport::on_profile_changed() { request_refresh(); }

void NVScreenEffectViewport::_notification(int p_what) {
	NVEffectViewport::_notification(p_what);
	if (p_what == NOTIFICATION_ENTER_TREE) {
		if (analog_profile.is_null()) {
			Ref<NVAnalogProfile> default_profile;
			default_profile.instantiate();
			set_analog_profile(default_profile);
		}
		request_refresh();
	} else if (p_what == NOTIFICATION_RESIZED || p_what == NOTIFICATION_TRANSFORM_CHANGED) {
		request_refresh();
	}
}

void NVScreenEffectViewport::request_refresh() {
	if (!is_inside_tree() || refresh_queued) { return; }
	refresh_queued = true;
	call_deferred("refresh_effect");
}

void NVScreenEffectViewport::upload_analog_profile() {
	if (analog_profile.is_null()) { return; }
#define NV_UPLOAD_ANALOG(name, shader_name) effect_material->set_shader_parameter(shader_name, analog_profile->get_##name())
	NV_UPLOAD_ANALOG(brightness, "grade_brightness");
	NV_UPLOAD_ANALOG(contrast, "grade_contrast");
	NV_UPLOAD_ANALOG(saturation, "grade_saturation");
	NV_UPLOAD_ANALOG(scanline_strength, "scanline_strength");
	NV_UPLOAD_ANALOG(scanline_period, "scanline_period");
	NV_UPLOAD_ANALOG(grain_strength, "grain_strength");
	NV_UPLOAD_ANALOG(grain_scale, "grain_scale");
	NV_UPLOAD_ANALOG(grain_speed, "grain_speed");
	NV_UPLOAD_ANALOG(vignette_strength, "vignette_strength");
	NV_UPLOAD_ANALOG(reflection_color, "reflection_color");
	NV_UPLOAD_ANALOG(reflection_center, "reflection_center");
	NV_UPLOAD_ANALOG(reflection_radius, "reflection_radius");
	NV_UPLOAD_ANALOG(rolling_strength, "rolling_strength");
	NV_UPLOAD_ANALOG(rolling_speed, "rolling_speed");
	NV_UPLOAD_ANALOG(rolling_width, "rolling_width");
	NV_UPLOAD_ANALOG(flicker_strength, "flicker_strength");
	NV_UPLOAD_ANALOG(flicker_frequency, "flicker_frequency");
	NV_UPLOAD_ANALOG(motion_enabled, "motion_enabled");
	NV_UPLOAD_ANALOG(overlay_blend, "overlay_blend");
#undef NV_UPLOAD_ANALOG
}

bool NVScreenEffectViewport::has_warp() const {
	return warp_profile.is_valid() &&
		(std::abs(warp_profile->get_radial_strength()) > 0.000001 || warp_profile->has_usable_displacement_map());
}

void NVScreenEffectViewport::refresh_effect() {
	refresh_queued = false;
	if (!is_inside_tree()) { return; }
	get_content_viewport();
	if (effect_material.is_null()) {
		effect_material.instantiate();
		Ref<Shader> shader = ResourceLoader::get_singleton()->load(
			"res://addons/night_voyageExt/shaders/screen_effect.gdshader", "Shader");
		effect_material->set_shader(shader);
		set_material(effect_material);
	}
	const Vector2 size = get_size();
	effect_material->set_shader_parameter("viewport_size", size);
	const bool map_valid = warp_profile.is_valid() && warp_profile->has_usable_displacement_map();
	effect_material->set_shader_parameter("radial_strength", warp_profile.is_valid() ? warp_profile->get_radial_strength() : 0.0);
	effect_material->set_shader_parameter("warp_center", warp_profile.is_valid() ? warp_profile->get_center() : Vector2(0.5, 0.5));
	effect_material->set_shader_parameter("use_displacement_map", map_valid);
	effect_material->set_shader_parameter("displacement_map", map_valid ? warp_profile->get_sampling_map() : Ref<Texture2D>());
	effect_material->set_shader_parameter("displacement_scale_px", warp_profile.is_valid() ? warp_profile->get_displacement_scale_px() : Vector2());
	upload_analog_profile();
	queue_redraw();
}

bool NVScreenEffectViewport::_has_point(const Vector2 &p_point) const {
	if (has_warp()) { return warp_profile->covers_display_point(p_point, get_size()); }
	return Control::_has_point(p_point);
}

void NVScreenEffectViewport::_gui_input(const Ref<InputEvent> &p_event) {
	if (!has_warp() || p_event.is_null()) { return; }
	Vector2 display_position;
	Vector2 display_relative;
	bool positional = false;
	if (const InputEventMouse *mouse = Object::cast_to<InputEventMouse>(p_event.ptr())) {
		display_position = mouse->get_position();
		positional = true;
	} else if (const InputEventScreenTouch *touch = Object::cast_to<InputEventScreenTouch>(p_event.ptr())) {
		display_position = touch->get_position();
		positional = true;
	} else if (const InputEventScreenDrag *drag = Object::cast_to<InputEventScreenDrag>(p_event.ptr())) {
		display_position = drag->get_position();
		display_relative = drag->get_relative();
		positional = true;
	} else if (const InputEventGesture *gesture = Object::cast_to<InputEventGesture>(p_event.ptr())) {
		display_position = gesture->get_position();
		positional = true;
	}
	if (!positional) {
		// 位移开启时由本节点拦截 GUI 事件，因此键盘、手柄等非位置输入要显式送入内容视口。
		get_content_viewport()->push_input(p_event);
		accept_event();
		return;
	}
	if (const InputEventMouseMotion *motion = Object::cast_to<InputEventMouseMotion>(p_event.ptr())) {
		display_position = motion->get_position();
		display_relative = motion->get_relative();
	}

	const Vector2 source_position = warp_profile->map_display_to_content(display_position, get_size());
	// 即使映射落到画面外也转发事件，使子视口能收到拖拽中的释放并清除按压状态。
	// clone+xformed_by 避免修改主 Viewport 的原始事件；平移量与输出 UV 的采样偏移一致。
	Transform2D translation;
	translation.set_origin(source_position - display_position);
	Ref<InputEvent> mapped = p_event->xformed_by(translation);
	if (InputEventMouseMotion *motion = Object::cast_to<InputEventMouseMotion>(mapped.ptr())) {
		const Vector2 mapped_start = source_position;
		const Vector2 mapped_end = warp_profile->map_display_to_content(display_position - display_relative, get_size());
		motion->set_relative(mapped_start - mapped_end);
	}
	if (InputEventScreenDrag *drag = Object::cast_to<InputEventScreenDrag>(mapped.ptr())) {
		const Vector2 mapped_start = source_position;
		const Vector2 mapped_end = warp_profile->map_display_to_content(display_position - display_relative, get_size());
		drag->set_relative(mapped_start - mapped_end);
	}
	get_content_viewport()->push_input(mapped);
	accept_event();
}

} // namespace godot
