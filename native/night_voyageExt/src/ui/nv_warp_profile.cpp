#include "nv_warp_profile.h"

#include <godot_cpp/classes/image.hpp>
#include <godot_cpp/core/class_db.hpp>
#include <algorithm>
#include <cmath>

namespace godot {

void NVWarpProfile::_bind_methods() {
	ClassDB::bind_method(D_METHOD("set_radial_strength", "strength"), &NVWarpProfile::set_radial_strength);
	ClassDB::bind_method(D_METHOD("get_radial_strength"), &NVWarpProfile::get_radial_strength);
	ClassDB::bind_method(D_METHOD("set_center", "center"), &NVWarpProfile::set_center);
	ClassDB::bind_method(D_METHOD("get_center"), &NVWarpProfile::get_center);
	ClassDB::bind_method(D_METHOD("set_displacement_map", "texture"), &NVWarpProfile::set_displacement_map);
	ClassDB::bind_method(D_METHOD("get_displacement_map"), &NVWarpProfile::get_displacement_map);
	ClassDB::bind_method(D_METHOD("set_displacement_scale_px", "scale"), &NVWarpProfile::set_displacement_scale_px);
	ClassDB::bind_method(D_METHOD("get_displacement_scale_px"), &NVWarpProfile::get_displacement_scale_px);
	ClassDB::bind_method(D_METHOD("refresh_map_cache"), &NVWarpProfile::refresh_map_cache);
	ClassDB::bind_method(D_METHOD("map_display_to_content", "position", "size"), &NVWarpProfile::map_display_to_content);
	ClassDB::bind_method(D_METHOD("covers_display_point", "position", "size"), &NVWarpProfile::covers_display_point);
	ClassDB::bind_method(D_METHOD("_on_map_changed"), &NVWarpProfile::on_map_changed);
	ADD_GROUP("Radial Warp", "");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "radial_strength", PROPERTY_HINT_RANGE, "-0.5,0.5,0.001"), "set_radial_strength", "get_radial_strength");
	ADD_PROPERTY(PropertyInfo(Variant::VECTOR2, "center", PROPERTY_HINT_RANGE, "-1,2,0.001,or_greater,or_less"), "set_center", "get_center");
	ADD_GROUP("Displacement Map", "");
	ADD_PROPERTY(PropertyInfo(Variant::OBJECT, "displacement_map", PROPERTY_HINT_RESOURCE_TYPE, "Texture2D"), "set_displacement_map", "get_displacement_map");
	ADD_PROPERTY(PropertyInfo(Variant::VECTOR2, "displacement_scale_px", PROPERTY_HINT_NONE, "suffix:px"), "set_displacement_scale_px", "get_displacement_scale_px");
}

void NVWarpProfile::set_radial_strength(double p_value) {
	if (!std::isfinite(p_value)) { return; }
	p_value = std::clamp(p_value, -0.5, 0.5);
	if (radial_strength != p_value) { radial_strength = p_value; emit_changed(); }
}

void NVWarpProfile::set_center(Vector2 p_value) {
	if (!p_value.is_finite()) { return; }
	p_value.x = std::clamp(double(p_value.x), -1.0, 2.0);
	p_value.y = std::clamp(double(p_value.y), -1.0, 2.0);
	if (center != p_value) { center = p_value; emit_changed(); }
}

void NVWarpProfile::set_displacement_map(const Ref<Texture2D> &p_texture) {
	if (displacement_map == p_texture) { return; }
	const Callable callback(this, "_on_map_changed");
	if (displacement_map.is_valid() && displacement_map->is_connected("changed", callback)) {
		displacement_map->disconnect("changed", callback);
	}
	displacement_map = p_texture;
	if (displacement_map.is_valid()) { displacement_map->connect("changed", callback); }
	rebuild_map_cache();
	emit_changed();
}

void NVWarpProfile::set_displacement_scale_px(Vector2 p_value) {
	if (!p_value.is_finite()) { return; }
	p_value.x = std::clamp(double(p_value.x), -2048.0, 2048.0);
	p_value.y = std::clamp(double(p_value.y), -2048.0, 2048.0);
	if (displacement_scale_px != p_value) { displacement_scale_px = p_value; emit_changed(); }
}

void NVWarpProfile::on_map_changed() {
	rebuild_map_cache();
	emit_changed();
}

void NVWarpProfile::refresh_map_cache() {
	rebuild_map_cache();
	// 显式刷新会替换 GPU 采样纹理，通知屏幕节点更新 Shader 参数。
	emit_changed();
}

void NVWarpProfile::rebuild_map_cache() {
	cached_map_size = Vector2i();
	cached_rg.clear();
	sampling_map.unref();
	if (displacement_map.is_null()) { return; }
	// CPU 与 GPU 使用同一份限尺寸图像，避免大图降采样后点击坐标与画面采样分叉。
	Ref<Image> source = displacement_map->get_image();
	if (source.is_null() || source->is_empty()) { return; }
	Ref<Image> image;
	image.instantiate();
	image->copy_from(source);
	if (image->is_compressed()) { image->decompress(); }
	image->convert(Image::FORMAT_RGBA8);
	const Vector2i source_size = image->get_size();
	const int longest = std::max(source_size.x, source_size.y);
	if (longest > 512) {
		const double scale = 512.0 / longest;
		image->resize(std::max(1, int(std::round(source_size.x * scale))),
			std::max(1, int(std::round(source_size.y * scale))), Image::INTERPOLATE_BILINEAR);
	}
	cached_map_size = image->get_size();
	sampling_map = ImageTexture::create_from_image(image);
	const PackedByteArray rgba = image->get_data();
	const int pixel_count = cached_map_size.x * cached_map_size.y;
	cached_rg.resize(pixel_count * 2);
	for (int i = 0; i < pixel_count; ++i) {
		cached_rg.set(i * 2, rgba[i * 4]);
		cached_rg.set(i * 2 + 1, rgba[i * 4 + 1]);
	}
}

Vector2 NVWarpProfile::sample_displacement(const Vector2 &p_uv) const {
	if (cached_rg.is_empty() || cached_map_size.x <= 0 || cached_map_size.y <= 0) { return Vector2(); }
	// GPU 线性过滤按 uv * 尺寸 - 0.5 定位像素中心；CPU 采用相同寻址与边界钳位。
	const double px = std::clamp(double(p_uv.x) * cached_map_size.x - 0.5, 0.0, double(cached_map_size.x - 1));
	const double py = std::clamp(double(p_uv.y) * cached_map_size.y - 0.5, 0.0, double(cached_map_size.y - 1));
	const int x0 = int(std::floor(px));
	const int y0 = int(std::floor(py));
	const int x1 = std::min(x0 + 1, cached_map_size.x - 1);
	const int y1 = std::min(y0 + 1, cached_map_size.y - 1);
	const double tx = px - x0;
	const double ty = py - y0;
	auto sample = [this](int x, int y) {
		const int index = (y * cached_map_size.x + x) * 2;
		return Vector2(double(cached_rg[index]), double(cached_rg[index + 1])) / 255.0;
	};
	const Vector2 top = sample(x0, y0) * (1.0 - tx) + sample(x1, y0) * tx;
	const Vector2 bottom = sample(x0, y1) * (1.0 - tx) + sample(x1, y1) * tx;
	return (top * (1.0 - ty) + bottom * ty - Vector2(0.5, 0.5)) * displacement_scale_px;
}

Vector2 NVWarpProfile::map_display_to_content(const Vector2 &p_display_position, const Vector2 &p_size) const {
	if (p_size.x <= 0.0 || p_size.y <= 0.0) { return Vector2(-1, -1); }
	Vector2 uv = p_display_position / p_size;
	const double aspect = double(p_size.x) / p_size.y;
	const Vector2 radial = (uv - center) * Vector2(aspect, 1.0);
	const double radius_squared = radial.length_squared();
	uv = center + (uv - center) * (1.0 + radial_strength * radius_squared);
	uv += sample_displacement(p_display_position / p_size) / p_size;
	return uv * p_size;
}

bool NVWarpProfile::covers_display_point(const Vector2 &p_display_position, const Vector2 &p_size) const {
	if (p_display_position.x < 0.0 || p_display_position.y < 0.0 ||
		p_display_position.x >= p_size.x || p_display_position.y >= p_size.y) { return false; }
	const Vector2 point = map_display_to_content(p_display_position, p_size);
	return point.x >= 0.0 && point.y >= 0.0 && point.x < p_size.x && point.y < p_size.y;
}

} // namespace godot
