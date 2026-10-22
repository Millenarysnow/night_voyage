#pragma once

#include "nv_analog_profile.h"
#include "nv_effect_viewport.h"
#include "nv_warp_profile.h"
#include <godot_cpp/classes/input_event.hpp>
#include <godot_cpp/classes/shader_material.hpp>

namespace godot {

/**
 * 将 Viewport 中的整组 UI 做屏幕变形和 CRT 质感，再显示到主画布。
 * 对位置输入使用与屏幕 Shader 同一映射；键盘等非位置输入沿用 SubViewportContainer。
 */
class NVScreenEffectViewport : public NVEffectViewport {
	GDCLASS(NVScreenEffectViewport, NVEffectViewport)

	Ref<NVWarpProfile> warp_profile;
	Ref<NVAnalogProfile> analog_profile;
	Ref<ShaderMaterial> effect_material;
	bool refresh_queued = false;

	void on_profile_changed();
	void request_refresh();
	void upload_analog_profile();
	bool has_warp() const;

protected:
	static void _bind_methods();
	void _notification(int p_what);
	void refresh_effect();

public:
	NVScreenEffectViewport();
	virtual bool _has_point(const Vector2 &p_point) const override;
	virtual void _gui_input(const Ref<InputEvent> &p_event) override;
	void set_warp_profile(const Ref<NVWarpProfile> &p_profile);
	Ref<NVWarpProfile> get_warp_profile() const { return warp_profile; }
	void set_analog_profile(const Ref<NVAnalogProfile> &p_profile);
	Ref<NVAnalogProfile> get_analog_profile() const { return analog_profile; }
};

} // namespace godot
