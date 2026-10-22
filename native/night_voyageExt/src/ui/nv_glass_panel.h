#pragma once

#include "nv_surface.h"
#include <godot_cpp/classes/back_buffer_copy.hpp>

namespace godot {

/**
 * 背景采样与 UI 内容分离：只有自身表面进入模糊，普通子节点保持清晰。
 * Local Capture 适合重叠玻璃；Shared Backdrop 适合同层共享同一背景的批量玻璃。
 * 共享模式由用户放置 BackBufferCopy 控制边界，不隐式地重复全屏复制。
 */
class NVGlassPanel : public NVSurface {
	GDCLASS(NVGlassPanel, NVSurface)

	double blur_radius = 16.0;
	double saturation = 1.1;
	double brightness = 1.0;
	bool high_quality = false;
	bool local_capture = true;
	BackBufferCopy *capture = nullptr;

protected:
	static void _bind_methods();
	void _notification(int p_what);
	String shader_path() const override;
	Ref<NVSurfaceStyle> create_default_style() const override;
	void update_effect() override;

public:
	NVGlassPanel();
#define NV_GLASS_ACCESSOR(type, name) \
	void set_##name(type p_value); \
	type get_##name() const { return name; }
	NV_GLASS_ACCESSOR(double, blur_radius)
	NV_GLASS_ACCESSOR(double, saturation)
	NV_GLASS_ACCESSOR(double, brightness)
	NV_GLASS_ACCESSOR(bool, high_quality)
	NV_GLASS_ACCESSOR(bool, local_capture)
#undef NV_GLASS_ACCESSOR
};

} // namespace godot
