#include "nv_effect_viewport.h"

#include <godot_cpp/classes/engine.hpp>
#include <godot_cpp/classes/scene_tree.hpp>
#include <godot_cpp/core/class_db.hpp>

namespace godot {

NVEffectViewport::NVEffectViewport() {
	set_stretch(true);
	set_mouse_filter(MOUSE_FILTER_STOP);
	set_mouse_target(true);
}

void NVEffectViewport::_bind_methods() {
	ClassDB::bind_method(D_METHOD("get_content_viewport"), &NVEffectViewport::get_content_viewport);
	ClassDB::bind_method(D_METHOD("get_content_root"), &NVEffectViewport::get_content_root);
}

void NVEffectViewport::_notification(int p_what) {
	if (p_what == NOTIFICATION_ENTER_TREE) {
		ensure_content_viewport();
	}
}

void NVEffectViewport::ensure_content_viewport() {
	for (int i = 0; i < get_child_count(); ++i) {
		SubViewport *candidate = Object::cast_to<SubViewport>(get_child(i));
		if (candidate) { content_viewport = candidate; break; }
	}
	if (!content_viewport) {
		content_viewport = memnew(SubViewport);
		content_viewport->set_name("EffectViewport");
		add_child(content_viewport);
		// 新建场景中生成的节点也保存在所属 PackedScene 中，编辑器可以继续添加内容。
		if (Engine::get_singleton()->is_editor_hint()) {
			Node *scene_owner = get_owner() ? get_owner() : this;
			content_viewport->set_owner(scene_owner);
		}
	}
	content_viewport->set_disable_3d(true);
	content_viewport->set_transparent_background(true);
	content_viewport->set_disable_input(false);
	// 不可见组件无需持续重绘；再次显示时仍按父容器可见性自动恢复更新。
	content_viewport->set_update_mode(SubViewport::UPDATE_WHEN_PARENT_VISIBLE);

	for (int i = 0; i < content_viewport->get_child_count(); ++i) {
		Control *candidate = Object::cast_to<Control>(content_viewport->get_child(i));
		if (candidate) { content_root = candidate; break; }
	}
	if (!content_root) {
		content_root = memnew(Control);
		content_root->set_name("Content");
		content_root->set_anchors_and_offsets_preset(Control::PRESET_FULL_RECT);
		content_viewport->add_child(content_root);
		if (Engine::get_singleton()->is_editor_hint()) {
			Node *scene_owner = get_owner() ? get_owner() : this;
			content_root->set_owner(scene_owner);
		}
	}
}

SubViewport *NVEffectViewport::get_content_viewport() {
	if (!content_viewport) { ensure_content_viewport(); }
	return content_viewport;
}

Control *NVEffectViewport::get_content_root() {
	if (!content_root) { ensure_content_viewport(); }
	return content_root;
}

} // namespace godot
