#include "nv_hit_shape.h"

#include <godot_cpp/core/class_db.hpp>
#include <algorithm>
#include <cmath>

namespace godot {

void NVHitShape::_bind_methods() {
	ClassDB::bind_method(D_METHOD("set_shape_type", "type"), &NVHitShape::set_shape_type);
	ClassDB::bind_method(D_METHOD("get_shape_type"), &NVHitShape::get_shape_type);
	ClassDB::bind_method(D_METHOD("set_corner_radii", "radii"), &NVHitShape::set_corner_radii);
	ClassDB::bind_method(D_METHOD("get_corner_radii"), &NVHitShape::get_corner_radii);
	ClassDB::bind_method(D_METHOD("set_alpha_mask", "texture"), &NVHitShape::set_alpha_mask);
	ClassDB::bind_method(D_METHOD("get_alpha_mask"), &NVHitShape::get_alpha_mask);
	ClassDB::bind_method(D_METHOD("set_alpha_threshold", "threshold"), &NVHitShape::set_alpha_threshold);
	ClassDB::bind_method(D_METHOD("get_alpha_threshold"), &NVHitShape::get_alpha_threshold);
	ClassDB::bind_method(D_METHOD("set_hit_slop", "slop"), &NVHitShape::set_hit_slop);
	ClassDB::bind_method(D_METHOD("get_hit_slop"), &NVHitShape::get_hit_slop);
	ClassDB::bind_method(D_METHOD("refresh_alpha_cache"), &NVHitShape::refresh_alpha_cache);
	ClassDB::bind_method(D_METHOD("contains", "point", "size"), &NVHitShape::contains);
	ClassDB::bind_method(D_METHOD("_on_mask_changed"), &NVHitShape::on_mask_changed);
	ADD_PROPERTY(PropertyInfo(Variant::INT, "shape_type", PROPERTY_HINT_ENUM, "Rounded Rect,Alpha Mask"), "set_shape_type", "get_shape_type");
	ADD_PROPERTY(PropertyInfo(Variant::VECTOR4, "corner_radii"), "set_corner_radii", "get_corner_radii");
	ADD_PROPERTY(PropertyInfo(Variant::OBJECT, "alpha_mask", PROPERTY_HINT_RESOURCE_TYPE, "Texture2D"), "set_alpha_mask", "get_alpha_mask");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "alpha_threshold", PROPERTY_HINT_RANGE, "0,1,0.01"), "set_alpha_threshold", "get_alpha_threshold");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "hit_slop", PROPERTY_HINT_RANGE, "-64,64,0.1,suffix:px"), "set_hit_slop", "get_hit_slop");
	BIND_ENUM_CONSTANT(SHAPE_ROUNDED_RECT);
	BIND_ENUM_CONSTANT(SHAPE_ALPHA_MASK);
}

void NVHitShape::set_shape_type(ShapeType p_type) {
	if (p_type < SHAPE_ROUNDED_RECT || p_type > SHAPE_ALPHA_MASK || shape_type == p_type) { return; }
	shape_type = p_type;
	if (shape_type == SHAPE_ALPHA_MASK && cached_alpha.is_empty()) { rebuild_alpha_cache(); }
	emit_changed();
}

void NVHitShape::set_corner_radii(const Vector4 &p_radii) {
	if (!p_radii.is_finite()) { return; }
	const Vector4 bounded = p_radii.clamp(Vector4(), Vector4(4096, 4096, 4096, 4096));
	if (corner_radii == bounded) { return; }
	corner_radii = bounded;
	emit_changed();
}

void NVHitShape::set_alpha_mask(const Ref<Texture2D> &p_texture) {
	if (alpha_mask == p_texture) { return; }
	const Callable callback(this, "_on_mask_changed");
	if (alpha_mask.is_valid() && alpha_mask->is_connected("changed", callback)) {
		alpha_mask->disconnect("changed", callback);
	}
	alpha_mask = p_texture;
	if (alpha_mask.is_valid()) { alpha_mask->connect("changed", callback); }
	rebuild_alpha_cache();
	emit_changed();
}

void NVHitShape::set_alpha_threshold(double p_threshold) {
	if (!std::isfinite(p_threshold)) { return; }
	p_threshold = std::clamp(p_threshold, 0.0, 1.0);
	if (alpha_threshold == p_threshold) { return; }
	alpha_threshold = p_threshold;
	emit_changed();
}

void NVHitShape::set_hit_slop(double p_slop) {
	if (!std::isfinite(p_slop)) { return; }
	p_slop = std::clamp(p_slop, -128.0, 128.0);
	if (hit_slop == p_slop) { return; }
	hit_slop = p_slop;
	emit_changed();
}

void NVHitShape::on_mask_changed() {
	rebuild_alpha_cache();
	emit_changed();
}

void NVHitShape::refresh_alpha_cache() { rebuild_alpha_cache(); }

void NVHitShape::rebuild_alpha_cache() {
	cached_alpha.clear();
	cached_mask_size = Vector2i();
	sampling_mask.unref();
	if (alpha_mask.is_null()) { return; }
	// CPU 命中缓存与 Shader 采样图在同一时机、从同一像素数据生成，事件中不读回 GPU。
	Ref<Image> source = alpha_mask->get_image();
	if (source.is_null() || source->is_empty()) { return; }
	Ref<Image> image;
	image.instantiate();
	image->copy_from(source);
	if (image->is_compressed()) { image->decompress(); }
	image->convert(Image::FORMAT_RGBA8);
	const Vector2i source_size = image->get_size();
	const int longest = std::max(source_size.x, source_size.y);
	if (longest > 1024) {
		const double scale = 1024.0 / longest;
		image->resize(std::max(1, int(std::round(source_size.x * scale))),
			std::max(1, int(std::round(source_size.y * scale))), Image::INTERPOLATE_BILINEAR);
	}
	cached_mask_size = image->get_size();
	sampling_mask = ImageTexture::create_from_image(image);
	const PackedByteArray rgba = image->get_data();
	const int pixel_count = cached_mask_size.x * cached_mask_size.y;
	cached_alpha.resize(pixel_count);
	for (int i = 0; i < pixel_count; ++i) {
		cached_alpha.set(i, rgba[i * 4 + 3]);
	}
}

bool NVHitShape::contains(const Vector2 &p_point, const Vector2 &p_size) const {
	if (p_size.x <= 0.0 || p_size.y <= 0.0) { return false; }
	if (shape_type == SHAPE_ALPHA_MASK) {
		if (p_point.x < 0.0 || p_point.y < 0.0 || p_point.x >= p_size.x || p_point.y >= p_size.y ||
			cached_alpha.is_empty() || cached_mask_size.x <= 0 || cached_mask_size.y <= 0) { return false; }
		// 与 GPU 的双线性寻址一致：UV 对应纹素中心坐标，不采用最近点量化。
		const double px = std::clamp(double(p_point.x / p_size.x) * cached_mask_size.x - 0.5,
			0.0, double(cached_mask_size.x - 1));
		const double py = std::clamp(double(p_point.y / p_size.y) * cached_mask_size.y - 0.5,
			0.0, double(cached_mask_size.y - 1));
		const int x0 = int(std::floor(px));
		const int y0 = int(std::floor(py));
		const int x1 = std::min(x0 + 1, cached_mask_size.x - 1);
		const int y1 = std::min(y0 + 1, cached_mask_size.y - 1);
		const double tx = px - x0;
		const double ty = py - y0;
		auto sample = [this](int x, int y) { return double(cached_alpha[y * cached_mask_size.x + x]) / 255.0; };
		const double top = sample(x0, y0) * (1.0 - tx) + sample(x1, y0) * tx;
		const double bottom = sample(x0, y1) * (1.0 - tx) + sample(x1, y1) * tx;
		return top * (1.0 - ty) + bottom * ty >= alpha_threshold;
	}

	const Vector2 center = p_size * 0.5;
	// 四个角的选择顺序与 NVSurface 共用 Shader 一致，并限制到短边的一半。
	const double radius_value = p_point.x < center.x ?
		(p_point.y < center.y ? corner_radii.x : corner_radii.w) :
		(p_point.y < center.y ? corner_radii.y : corner_radii.z);
	const double radius = std::clamp(radius_value, 0.0, std::min(p_size.x, p_size.y) * 0.5);
	const Vector2 corner_q = (p_point - center).abs() - center + Vector2(radius, radius);
	const double distance = Vector2(std::max(corner_q.x, real_t(0)), std::max(corner_q.y, real_t(0))).length() +
		std::min(std::max(corner_q.x, corner_q.y), real_t(0)) - radius;
	return distance <= hit_slop;
}

} // namespace godot
