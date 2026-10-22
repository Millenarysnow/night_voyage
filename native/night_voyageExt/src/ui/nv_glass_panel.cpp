#include "nv_glass_panel.h"

#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/classes/viewport.hpp>
#include <algorithm>
#include <cmath>

namespace godot {

NVGlassPanel::NVGlassPanel() {
	set_notify_transform(true);
}

void NVGlassPanel::_bind_methods() {
#define NV_BIND_GLASS(type, name, hint, text) \
	ClassDB::bind_method(D_METHOD("set_" #name, "value"), &NVGlassPanel::set_##name); \
	ClassDB::bind_method(D_METHOD("get_" #name), &NVGlassPanel::get_##name); \
	ADD_PROPERTY(PropertyInfo(type, #name, hint, text), "set_" #name, "get_" #name);
	ADD_GROUP("Backdrop", "");
	NV_BIND_GLASS(Variant::FLOAT, blur_radius, PROPERTY_HINT_RANGE, "0,64,0.1")
	NV_BIND_GLASS(Variant::FLOAT, saturation, PROPERTY_HINT_RANGE, "0,2,0.01")
	NV_BIND_GLASS(Variant::FLOAT, brightness, PROPERTY_HINT_RANGE, "0,2,0.01")
	NV_BIND_GLASS(Variant::BOOL, high_quality, PROPERTY_HINT_NONE, "")
	NV_BIND_GLASS(Variant::BOOL, local_capture, PROPERTY_HINT_NONE, "")
#undef NV_BIND_GLASS
}

#define NV_GLASS_SCALAR(name, lower, upper) \
	void NVGlassPanel::set_##name(double p_value) { \
		if (!std::isfinite(p_value)) { return; } \
		p_value = std::clamp(p_value, double(lower), double(upper)); \
		if (name == p_value) { return; } \
		name = p_value; \
		request_refresh(); \
	}
NV_GLASS_SCALAR(blur_radius, 0, 64)
NV_GLASS_SCALAR(saturation, 0, 2)
NV_GLASS_SCALAR(brightness, 0, 2)
#undef NV_GLASS_SCALAR

void NVGlassPanel::set_high_quality(bool p_value) {
	if (high_quality != p_value) { high_quality = p_value; request_refresh(); }
}

void NVGlassPanel::set_local_capture(bool p_value) {
	if (local_capture != p_value) { local_capture = p_value; request_refresh(); }
}

void NVGlassPanel::_notification(int p_what) {
	if (p_what == NOTIFICATION_ENTER_TREE) {
		// 窗口拉伸改变 Viewport 的像素变换，也需要更新屏幕复制区域。
		const Callable callback(this, "_request_refresh");
		if (!get_viewport()->is_connected("size_changed", callback)) {
			get_viewport()->connect("size_changed", callback);
		}
	} else if (p_what == NOTIFICATION_EXIT_TREE) {
		const Callable callback(this, "_request_refresh");
		if (get_viewport()->is_connected("size_changed", callback)) {
			get_viewport()->disconnect("size_changed", callback);
		}
	} else if (p_what == NOTIFICATION_TRANSFORM_CHANGED) {
		// 控件缩放后需要重新计算局部复制边界；稳定画面没有每帧 CPU 工作。
		request_refresh();
	}
}

String NVGlassPanel::shader_path() const {
	return "res://addons/night_voyageExt/shaders/frosted_glass.gdshader";
}

Ref<NVSurfaceStyle> NVGlassPanel::create_default_style() const {
	Ref<NVSurfaceStyle> style = NVSurface::create_default_style();
	// 玻璃的填充 Alpha 表示染色强度，普通 Surface 则表示表面不透明度。
	style->set_fill_color(Color(0.05, 0.07, 0.12, 0.35));
	return style;
}

void NVGlassPanel::update_effect() {
	NVSurface::update_effect();
	if (!capture) {
		capture = memnew(BackBufferCopy);
		capture->set_name("BackdropCapture");
		// draw_behind_parent 保证复制发生在父控件绘制之前，而非玻璃已经画完以后。
		capture->set_draw_behind_parent(true);
		add_child(capture, false, INTERNAL_MODE_FRONT);
	}
	capture->set_copy_mode(local_capture ? BackBufferCopy::COPY_MODE_RECT : BackBufferCopy::COPY_MODE_DISABLED);
	// 本机 4.8 beta 的 GLES3/RD 复制路径直接读取 rect，而非 cull 后的 screen_rect。
	// 将捕获节点变换抵消到实际屏幕像素空间，使这两种矩形在各后端保持一致。
	// 节点仍留在父 CanvasItem 的绘制链中，behind-parent 顺序不会被 top_level 打断。
	const Transform2D to_pixels = get_viewport_transform() * get_global_transform();
	if (std::abs(double(to_pixels.determinant())) > 0.000001) {
		capture->set_transform(to_pixels.affine_inverse());
		const Rect2 pixel_bounds = to_pixels.xform(Rect2(Vector2(), get_size()));
		// 多留一圈 3σ 支持区，避免 mip 采样读到局部复制范围以外的旧像素。
		capture->set_rect(pixel_bounds.grow(blur_radius * 3.0 + 32.0));
	} else {
		capture->set_copy_mode(BackBufferCopy::COPY_MODE_DISABLED);
	}
	effect_material->set_shader_parameter("blur_radius", blur_radius);
	effect_material->set_shader_parameter("saturation", saturation);
	effect_material->set_shader_parameter("brightness", brightness);
	effect_material->set_shader_parameter("high_quality", high_quality);
}

} // namespace godot
