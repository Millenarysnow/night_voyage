#pragma once

#include <godot_cpp/classes/resource.hpp>
#include <godot_cpp/classes/image_texture.hpp>
#include <godot_cpp/classes/texture2d.hpp>
#include <godot_cpp/variant/packed_byte_array.hpp>
#include <godot_cpp/variant/vector2.hpp>

namespace godot {

/**
 * 可复用的画面位移场。径向曲率与 RG 位移纹理可独立或叠加使用。
 * 输出像素到源图 UV 的映射同时供 GPU 采样和鼠标反算，交互不会脱离画面。
 */
class NVWarpProfile : public Resource {
	GDCLASS(NVWarpProfile, Resource)

	double radial_strength = 0.0;
	Vector2 center = Vector2(0.5, 0.5);
	Ref<Texture2D> displacement_map;
	Ref<ImageTexture> sampling_map;
	Vector2 displacement_scale_px = Vector2(84, 84);
	Vector2i cached_map_size;
	PackedByteArray cached_rg;

	void on_map_changed();
	void rebuild_map_cache();
	Vector2 sample_displacement(const Vector2 &p_uv) const;

protected:
	static void _bind_methods();

public:
	void set_radial_strength(double p_value);
	double get_radial_strength() const { return radial_strength; }
	void set_center(Vector2 p_value);
	Vector2 get_center() const { return center; }
	void set_displacement_map(const Ref<Texture2D> &p_texture);
	Ref<Texture2D> get_displacement_map() const { return displacement_map; }
	Ref<Texture2D> get_sampling_map() const { return sampling_map; }
	void set_displacement_scale_px(Vector2 p_value);
	Vector2 get_displacement_scale_px() const { return displacement_scale_px; }
	void refresh_map_cache();
	bool has_usable_displacement_map() const { return !cached_rg.is_empty() && sampling_map.is_valid(); }
	Vector2 map_display_to_content(const Vector2 &p_display_position, const Vector2 &p_size) const;
	bool covers_display_point(const Vector2 &p_display_position, const Vector2 &p_size) const;
};

} // namespace godot
