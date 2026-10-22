#pragma once

#include <godot_cpp/classes/control.hpp>
#include <godot_cpp/classes/shader_material.hpp>
#include <godot_cpp/classes/sub_viewport.hpp>
#include <godot_cpp/classes/texture2d.hpp>
#include <godot_cpp/classes/color_rect.hpp>

namespace godot {

/**
 * 按任意 Texture2D 的 Alpha 轮廓生成柔和投影，可直接显示原图或作为独立投影层。
 * 缓存流程：原图 Alpha → 横向模糊 → 纵向模糊 → 持久 GPU 纹理。
 * 两个低分辨率 SubViewport 在空闲时禁用；移动、旋转、颜色和偏移不重做模糊。
 * 不将图片读回 CPU，也不在 _process 中处理像素。
 */
class NVAlphaShadow : public Control {
	GDCLASS(NVAlphaShadow, Control)

	Ref<Texture2D> texture;
	Ref<ShaderMaterial> display_material;
	Ref<ShaderMaterial> horizontal_material;
	Ref<ShaderMaterial> vertical_material;
	SubViewport *horizontal_viewport = nullptr;
	SubViewport *vertical_viewport = nullptr;
	ColorRect *horizontal_quad = nullptr;
	ColorRect *vertical_quad = nullptr;

	Color shadow_color = Color(0, 0, 0, 0.65);
	Vector2 shadow_offset = Vector2(0, 14);
	double blur_radius = 16.0;
	double cache_scale = 0.5;
	int max_cache_size = 1024;
	bool show_source = true;
	double cache_padding = 0.0;
	double draw_padding = 0.0;
	Vector2 logical_cache_size;
	int dirty_flags = 0;
	bool refresh_queued = false;
	int render_stage = 0;
	int64_t cache_build_count = 0;

	void request_refresh(bool p_rebuild);
	void ensure_pipeline();
	void rebuild_cache();
	void update_display();
	void on_texture_changed();
	void on_post_draw();
	void stop_render_callback();

protected:
	static void _bind_methods();
	void _notification(int p_what);

public:
	NVAlphaShadow();
	~NVAlphaShadow() override;
	void set_texture(const Ref<Texture2D> &p_texture);
	Ref<Texture2D> get_texture() const { return texture; }
#define NV_ALPHA_ACCESSOR(type, name) \
	void set_##name(type p_value); \
	type get_##name() const { return name; }
	NV_ALPHA_ACCESSOR(Color, shadow_color)
	NV_ALPHA_ACCESSOR(Vector2, shadow_offset)
	NV_ALPHA_ACCESSOR(double, blur_radius)
	NV_ALPHA_ACCESSOR(double, cache_scale)
	NV_ALPHA_ACCESSOR(int, max_cache_size)
	NV_ALPHA_ACCESSOR(bool, show_source)
#undef NV_ALPHA_ACCESSOR
	void refresh_effect();
	void invalidate_cache();
	int64_t get_cache_build_count() const { return cache_build_count; }
	bool is_cache_pending() const { return render_stage != 0 || (dirty_flags & 2); }
	// 实际缓存像素尺寸不同于逻辑尺寸，便于验收显存上限和自适应降采样。
	Vector2i get_cache_size_pixels() const { return vertical_viewport ? vertical_viewport->get_size() : Vector2i(); }
	void _draw() override;
};

} // namespace godot
