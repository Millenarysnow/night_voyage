#pragma once

#include <godot_cpp/classes/image.hpp>
#include <godot_cpp/classes/image_texture.hpp>
#include <godot_cpp/classes/resource.hpp>
#include <godot_cpp/classes/texture2d.hpp>
#include <godot_cpp/variant/color.hpp>
#include <godot_cpp/variant/packed_byte_array.hpp>
#include <godot_cpp/variant/vector2.hpp>
#include <godot_cpp/variant/vector4.hpp>

namespace godot {

/**
 * 同时供 GPU 裁切和 GUI 命中测试使用的二维形状。
 * 圆角矩形按解析距离场精确判断；Alpha 遮罩在资源变化时预采样一次，
 * 鼠标移动时只查询 CPU 缓存，不从 GPU 读回纹理。
 */
class NVHitShape : public Resource {
	GDCLASS(NVHitShape, Resource)

public:
	enum ShapeType {
		SHAPE_ROUNDED_RECT = 0,
		SHAPE_ALPHA_MASK = 1,
	};

private:
	ShapeType shape_type = SHAPE_ROUNDED_RECT;
	Vector4 corner_radii = Vector4(16, 16, 16, 16);
	Ref<Texture2D> alpha_mask;
	Ref<ImageTexture> sampling_mask;
	double alpha_threshold = 0.5;
	double hit_slop = 0.0;
	Vector2i cached_mask_size;
	PackedByteArray cached_alpha;

	void rebuild_alpha_cache();
	void on_mask_changed();

protected:
	static void _bind_methods();

public:
	void set_shape_type(ShapeType p_type);
	ShapeType get_shape_type() const { return shape_type; }
	void set_corner_radii(const Vector4 &p_radii);
	Vector4 get_corner_radii() const { return corner_radii; }
	void set_alpha_mask(const Ref<Texture2D> &p_texture);
	Ref<Texture2D> get_alpha_mask() const { return alpha_mask; }
	Ref<Texture2D> get_sampling_mask() const { return sampling_mask; }
	void set_alpha_threshold(double p_threshold);
	double get_alpha_threshold() const { return alpha_threshold; }
	void set_hit_slop(double p_slop);
	double get_hit_slop() const { return hit_slop; }
	void refresh_alpha_cache();
	bool contains(const Vector2 &p_point, const Vector2 &p_size) const;
};

} // namespace godot

VARIANT_ENUM_CAST(godot::NVHitShape::ShapeType);
