#include "nv_clip_viewport.h"

#include <godot_cpp/classes/resource_loader.hpp>
#include <godot_cpp/classes/shader.hpp>
#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/variant/callable.hpp>

namespace godot {

NVClipViewport::NVClipViewport() = default;

void NVClipViewport::_bind_methods() {
	ClassDB::bind_method(D_METHOD("set_hit_shape", "shape"), &NVClipViewport::set_hit_shape);
	ClassDB::bind_method(D_METHOD("get_hit_shape"), &NVClipViewport::get_hit_shape);
	ClassDB::bind_method(D_METHOD("refresh_effect"), &NVClipViewport::refresh_effect);
	ClassDB::bind_method(D_METHOD("_on_shape_changed"), &NVClipViewport::on_shape_changed);
	ADD_PROPERTY(PropertyInfo(Variant::OBJECT, "hit_shape", PROPERTY_HINT_RESOURCE_TYPE, "NVHitShape"),
		"set_hit_shape", "get_hit_shape");
}

void NVClipViewport::set_hit_shape(const Ref<NVHitShape> &p_shape) {
	if (hit_shape == p_shape) { return; }
	const Callable callback(this, "_on_shape_changed");
	if (hit_shape.is_valid() && hit_shape->is_connected("changed", callback)) {
		hit_shape->disconnect("changed", callback);
	}
	hit_shape = p_shape;
	if (hit_shape.is_valid()) { hit_shape->connect("changed", callback); }
	request_refresh();
}

void NVClipViewport::_notification(int p_what) {
	NVEffectViewport::_notification(p_what);
	if (p_what == NOTIFICATION_ENTER_TREE) {
		if (hit_shape.is_null()) {
			Ref<NVHitShape> default_shape;
			default_shape.instantiate();
			set_hit_shape(default_shape);
		}
		request_refresh();
	} else if (p_what == NOTIFICATION_RESIZED) {
		request_refresh();
	}
}

void NVClipViewport::on_shape_changed() { request_refresh(); }

void NVClipViewport::request_refresh() {
	if (!is_inside_tree() || refresh_queued) { return; }
	refresh_queued = true;
	call_deferred("refresh_effect");
}

void NVClipViewport::refresh_effect() {
	refresh_queued = false;
	if (!is_inside_tree() || hit_shape.is_null()) { return; }
	if (clip_material.is_null()) {
		clip_material.instantiate();
		Ref<Shader> shader = ResourceLoader::get_singleton()->load(
			"res://addons/night_voyageExt/shaders/clip_viewport.gdshader", "Shader");
		clip_material->set_shader(shader);
		set_material(clip_material);
	}
	clip_material->set_shader_parameter("viewport_size", get_size());
	clip_material->set_shader_parameter("shape_type", int(hit_shape->get_shape_type()));
	clip_material->set_shader_parameter("corner_radii", hit_shape->get_corner_radii());
	clip_material->set_shader_parameter("alpha_mask", hit_shape->get_sampling_mask());
	clip_material->set_shader_parameter("alpha_threshold", hit_shape->get_alpha_threshold());
	queue_redraw();
}

bool NVClipViewport::_has_point(const Vector2 &p_point) const {
	return hit_shape.is_valid() ? hit_shape->contains(p_point, get_size()) : Control::_has_point(p_point);
}

} // namespace godot
