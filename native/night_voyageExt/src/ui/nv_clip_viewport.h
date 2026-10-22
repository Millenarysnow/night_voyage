#pragma once

#include "nv_effect_viewport.h"
#include "nv_hit_shape.h"
#include <godot_cpp/classes/shader_material.hpp>

namespace godot {

/**
 * 先将子 UI 合成到 SubViewport，再以同一个 NVHitShape 做圆角/Alpha 裁切与输入命中。
 * 普通静态图片无需此离屏容器，可直接使用 CanvasItem 材质裁切以节省渲染目标。
 */
class NVClipViewport : public NVEffectViewport {
	GDCLASS(NVClipViewport, NVEffectViewport)

	Ref<NVHitShape> hit_shape;
	Ref<ShaderMaterial> clip_material;
	bool refresh_queued = false;

	void on_shape_changed();
	void request_refresh();
protected:
	static void _bind_methods();
	void _notification(int p_what);
	void refresh_effect();

public:
	NVClipViewport();
	virtual bool _has_point(const Vector2 &p_point) const override;
	void set_hit_shape(const Ref<NVHitShape> &p_shape);
	Ref<NVHitShape> get_hit_shape() const { return hit_shape; }
};

} // namespace godot
