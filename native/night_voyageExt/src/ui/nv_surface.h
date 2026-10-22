#pragma once

#include "nv_surface_style.h"
#include <godot_cpp/classes/control.hpp>
#include <godot_cpp/classes/shader_material.hpp>

namespace godot {

/**
 * 一次四边形绘制完成圆角、填充、边框、外阴影、内阴影和光晕。
 * 直接继承 Control：子控件保留 Godot 原生锚点、容器、输入和焦点行为。
 * 该控件只绘制自己的表面，不隐式裁剪子项，也不把材质强加给子控件。
 */
class NVSurface : public Control {
	GDCLASS(NVSurface, Control)

	Ref<NVSurfaceStyle> surface_style;
	bool refresh_queued = false;

protected:
	Ref<ShaderMaterial> effect_material;
	double draw_padding = 0.0;
	static void _bind_methods();
	void _notification(int p_what);
	virtual String shader_path() const;
	virtual Ref<NVSurfaceStyle> create_default_style() const;
	virtual void update_effect();
	void request_refresh();

public:
	NVSurface();
	~NVSurface() override;
	void set_surface_style(const Ref<NVSurfaceStyle> &p_style);
	Ref<NVSurfaceStyle> get_surface_style() const { return surface_style; }
	void refresh_effect();
	void _draw() override;
};

} // namespace godot
