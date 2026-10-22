#pragma once

#include "nv_analog_profile.h"
#include <godot_cpp/classes/control.hpp>
#include <godot_cpp/classes/shader_material.hpp>
#include <godot_cpp/classes/texture2d.hpp>

namespace godot {

/**
 * 对单个视频帧/监控贴图做一遍颜色分级与模拟信号效果。
 * 需要把 VideoStreamPlayer 放入 SubViewport，再将该 ViewportTexture 作为输入。
 */
class NVFootageEffect : public Control {
	GDCLASS(NVFootageEffect, Control)

	Ref<Texture2D> footage;
	Ref<NVAnalogProfile> analog_profile;
	Ref<ShaderMaterial> effect_material;
	bool refresh_queued = false;

	void on_resource_changed();
	void request_refresh();

protected:
	static void _bind_methods();
	void _notification(int p_what);

public:
	NVFootageEffect();
	void set_footage(const Ref<Texture2D> &p_texture);
	Ref<Texture2D> get_footage() const { return footage; }
	void set_analog_profile(const Ref<NVAnalogProfile> &p_profile);
	Ref<NVAnalogProfile> get_analog_profile() const { return analog_profile; }
	void refresh_effect();
	void _draw() override;
};

} // namespace godot
