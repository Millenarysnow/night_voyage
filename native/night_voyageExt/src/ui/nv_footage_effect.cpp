#include "nv_footage_effect.h"

#include <godot_cpp/classes/resource_loader.hpp>
#include <godot_cpp/classes/shader.hpp>
#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/variant/callable.hpp>

namespace godot {

NVFootageEffect::NVFootageEffect() {
	set_mouse_filter(MOUSE_FILTER_IGNORE);
	set_process(false);
}

void NVFootageEffect::_bind_methods() {
	ClassDB::bind_method(D_METHOD("set_footage", "texture"), &NVFootageEffect::set_footage);
	ClassDB::bind_method(D_METHOD("get_footage"), &NVFootageEffect::get_footage);
	ClassDB::bind_method(D_METHOD("set_analog_profile", "profile"), &NVFootageEffect::set_analog_profile);
	ClassDB::bind_method(D_METHOD("get_analog_profile"), &NVFootageEffect::get_analog_profile);
	ClassDB::bind_method(D_METHOD("refresh_effect"), &NVFootageEffect::refresh_effect);
	ClassDB::bind_method(D_METHOD("_on_resource_changed"), &NVFootageEffect::on_resource_changed);
	ADD_PROPERTY(PropertyInfo(Variant::OBJECT, "footage", PROPERTY_HINT_RESOURCE_TYPE, "Texture2D"), "set_footage", "get_footage");
	ADD_PROPERTY(PropertyInfo(Variant::OBJECT, "analog_profile", PROPERTY_HINT_RESOURCE_TYPE, "NVAnalogProfile"), "set_analog_profile", "get_analog_profile");
}

void NVFootageEffect::set_footage(const Ref<Texture2D> &p_texture) {
	if (footage == p_texture) { return; }
	const Callable callback(this, "_on_resource_changed");
	if (footage.is_valid() && footage->is_connected("changed", callback)) { footage->disconnect("changed", callback); }
	footage = p_texture;
	if (footage.is_valid()) { footage->connect("changed", callback); }
	request_refresh();
}

void NVFootageEffect::set_analog_profile(const Ref<NVAnalogProfile> &p_profile) {
	if (analog_profile == p_profile) { return; }
	const Callable callback(this, "_on_resource_changed");
	if (analog_profile.is_valid() && analog_profile->is_connected("changed", callback)) { analog_profile->disconnect("changed", callback); }
	analog_profile = p_profile;
	if (analog_profile.is_valid()) { analog_profile->connect("changed", callback); }
	request_refresh();
}

void NVFootageEffect::on_resource_changed() { request_refresh(); }

void NVFootageEffect::_notification(int p_what) {
	if (p_what == NOTIFICATION_ENTER_TREE) {
		if (analog_profile.is_null()) {
			Ref<NVAnalogProfile> default_profile;
			default_profile.instantiate();
			set_analog_profile(default_profile);
		}
		request_refresh();
	} else if (p_what == NOTIFICATION_RESIZED) {
		queue_redraw();
	}
}

void NVFootageEffect::request_refresh() {
	if (!is_inside_tree() || refresh_queued) { return; }
	refresh_queued = true;
	call_deferred("refresh_effect");
}

void NVFootageEffect::refresh_effect() {
	refresh_queued = false;
	if (!is_inside_tree()) { return; }
	if (effect_material.is_null()) {
		effect_material.instantiate();
		Ref<Shader> shader = ResourceLoader::get_singleton()->load(
			"res://addons/night_voyageExt/shaders/footage_effect.gdshader", "Shader");
		effect_material->set_shader(shader);
		set_material(effect_material);
	}
	effect_material->set_shader_parameter("footage", footage);
	if (footage.is_valid()) {
		effect_material->set_shader_parameter("footage_size", Vector2(footage->get_width(), footage->get_height()));
	}
#define NV_UPLOAD_FOOTAGE(name, shader_name) effect_material->set_shader_parameter(shader_name, analog_profile->get_##name())
	NV_UPLOAD_FOOTAGE(brightness, "grade_brightness");
	NV_UPLOAD_FOOTAGE(contrast, "grade_contrast");
	NV_UPLOAD_FOOTAGE(saturation, "grade_saturation");
	NV_UPLOAD_FOOTAGE(scanline_strength, "scanline_strength");
	NV_UPLOAD_FOOTAGE(scanline_period, "scanline_period");
	NV_UPLOAD_FOOTAGE(grain_strength, "grain_strength");
	NV_UPLOAD_FOOTAGE(grain_scale, "grain_scale");
	NV_UPLOAD_FOOTAGE(grain_speed, "grain_speed");
	NV_UPLOAD_FOOTAGE(vignette_strength, "vignette_strength");
	NV_UPLOAD_FOOTAGE(reflection_color, "reflection_color");
	NV_UPLOAD_FOOTAGE(reflection_center, "reflection_center");
	NV_UPLOAD_FOOTAGE(reflection_radius, "reflection_radius");
	NV_UPLOAD_FOOTAGE(rolling_strength, "rolling_strength");
	NV_UPLOAD_FOOTAGE(rolling_speed, "rolling_speed");
	NV_UPLOAD_FOOTAGE(rolling_width, "rolling_width");
	NV_UPLOAD_FOOTAGE(flicker_strength, "flicker_strength");
	NV_UPLOAD_FOOTAGE(flicker_frequency, "flicker_frequency");
	NV_UPLOAD_FOOTAGE(motion_enabled, "motion_enabled");
	NV_UPLOAD_FOOTAGE(overlay_blend, "overlay_blend");
#undef NV_UPLOAD_FOOTAGE
	queue_redraw();
}

void NVFootageEffect::_draw() {
	if (footage.is_valid() && effect_material.is_valid() && get_size().x > 0 && get_size().y > 0) {
		draw_rect(Rect2(Vector2(), get_size()), Color(1, 1, 1, 1));
	}
}

} // namespace godot
