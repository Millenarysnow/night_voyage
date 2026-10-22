#include "nv_effects_demo.h"

#include <godot_cpp/classes/check_box.hpp>
#include <godot_cpp/classes/color_rect.hpp>
#include <godot_cpp/classes/dir_access.hpp>
#include <godot_cpp/classes/engine.hpp>
#include <godot_cpp/classes/h_slider.hpp>
#include <godot_cpp/classes/image.hpp>
#include <godot_cpp/classes/os.hpp>
#include <godot_cpp/classes/resource_loader.hpp>
#include <godot_cpp/classes/scene_tree.hpp>
#include <godot_cpp/classes/viewport_texture.hpp>
#include <godot_cpp/classes/window.hpp>
#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/variant/utility_functions.hpp>
#include <algorithm>
#include <cmath>

namespace godot {

void NVEffectsDemo::_bind_methods() {
	ClassDB::bind_method(D_METHOD("_blur_changed", "value"), &NVEffectsDemo::on_blur_changed);
	ClassDB::bind_method(D_METHOD("_quality_toggled", "enabled"), &NVEffectsDemo::on_quality_toggled);
	ClassDB::bind_method(D_METHOD("_shadow_changed", "value"), &NVEffectsDemo::on_shadow_changed);
}

Label *NVEffectsDemo::add_label(Node *p_parent, Vector2 p_position, Vector2 p_size,
	const String &p_text, int p_font_size, Color p_color) {
	Label *label = memnew(Label);
	label->set_position(p_position);
	label->set_size(p_size);
	label->set_text(p_text);
	label->add_theme_font_size_override("font_size", p_font_size);
	label->add_theme_color_override("font_color", p_color);
	label->set_mouse_filter(MOUSE_FILTER_IGNORE);
	p_parent->add_child(label);
	return label;
}

Ref<ImageTexture> NVEffectsDemo::make_contour_texture() {
	// 仅演示生成一次测试图。中心空洞与右侧缺口能直观看出投影来自 Alpha 而非包围盒。
	Ref<Image> image = Image::create_empty(160, 160, false, Image::FORMAT_RGBA8);
	for (int y = 0; y < 160; ++y) {
		for (int x = 0; x < 160; ++x) {
			const double dx = x + 0.5 - 80.0;
			const double dy = y + 0.5 - 80.0;
			const double distance = std::sqrt(dx * dx + dy * dy);
			const double ring = std::clamp(std::min(65.0 - distance, distance - 40.0) + 0.5, 0.0, 1.0);
			const double notch = (dx > 46.0 && std::abs(dy) < 10.0) ? 0.0 : 1.0;
			image->set_pixel(x, y, Color(0.2 + x / 240.0, 0.85, 0.75, ring * notch));
		}
	}
	return ImageTexture::create_from_image(image);
}

void NVEffectsDemo::_ready() {
	// 在编辑器里不创建运行时测试节点，避免编辑器加载场景就改窗口或执行测试。
	if (Engine::get_singleton()->is_editor_hint()) { set_process(false); return; }
	const PackedStringArray args = OS::get_singleton()->get_cmdline_user_args();
	validation = args.has("--nv-self-test");
	capture = args.has("--nv-capture");
	if (validation) {
#ifdef DEBUG_ENABLED
		UtilityFunctions::print("NV extension build: template_debug");
#else
		UtilityFunctions::print("NV extension build: template_release");
#endif
	}
	const Vector2i size = validation ? Vector2i(768, 560) : Vector2i(1280, 800);
	get_window()->set_size(size);
	get_window()->set_content_scale_size(size);
	get_window()->set_content_scale_mode(Window::CONTENT_SCALE_MODE_CANVAS_ITEMS);
	get_window()->set_title(validation ? "Night Voyage | GPU validation" : "Night Voyage | Native UI Effects");
	set_size(Vector2(size));
	contour_texture = make_contour_texture();
	if (validation) { build_validation(); } else { build_demo(); }
	set_process(validation || capture);
	queue_redraw();
}

void NVEffectsDemo::build_demo() {
	add_label(this, Vector2(48, 24), Vector2(1184, 48), "NIGHT VOYAGE / UI MATERIALS", 30);
	add_label(this, Vector2(48, 76), Vector2(1184, 30), "01   FROSTED GLASS", 16, Color(0.5, 0.7, 0.78));

	glass = memnew(NVGlassPanel);
	glass->set_position(Vector2(72, 148));
	glass->set_size(Vector2(482, 220));
	Ref<NVSurfaceStyle> glass_style = ResourceLoader::get_singleton()->load(
		"res://addons/night_voyageExt/presets/glass.tres");
	glass->set_surface_style(glass_style);
	add_child(glass);
	add_label(glass, Vector2(28, 28), Vector2(420, 34), "Behind the surface", 26);
	add_label(glass, Vector2(28, 80), Vector2(420, 32), "A blurred backdrop. A sharp foreground.", 19);
	add_label(glass, Vector2(28, 126), Vector2(420, 56), "NVGlassPanel  /  blur + tint + border\nNative Control children remain sharp.", 17);

	NVGlassPanel *overlap = memnew(NVGlassPanel);
	overlap->set_position(Vector2(466, 268));
	overlap->set_size(Vector2(262, 126));
	overlap->set_blur_radius(8);
	add_child(overlap);
	add_label(overlap, Vector2(22, 18), Vector2(220, 32), "Layered glass", 22);
	add_label(overlap, Vector2(22, 58), Vector2(220, 48), "Local capture includes\nthe earlier panel.", 17);
	add_label(this, Vector2(830, 164), Vector2(350, 42), "Shared materials", 24);
	add_label(this, Vector2(830, 216), Vector2(350, 120), "C++ GDExtension\nCanvasItem shaders\nEditable .tres style resources\nNo per-frame script polling", 18);

	add_label(this, Vector2(48, 446), Vector2(350, 30), "02   SHADOW + GLOW", 16, Color(0.5, 0.7, 0.78));
	auto surface = [this](Vector2 position, const char *preset) {
		NVSurface *node = memnew(NVSurface);
		node->set_position(position);
		node->set_size(Vector2(300, 146));
		node->set_surface_style(ResourceLoader::get_singleton()->load(preset));
		add_child(node);
		return node;
	};
	NVSurface *shadow = surface(Vector2(72, 504), "res://addons/night_voyageExt/presets/soft_shadow.tres");
	add_label(shadow, Vector2(24, 24), Vector2(252, 34), "Soft elevation", 24);
	add_label(shadow, Vector2(24, 72), Vector2(252, 54), "Outer + inner shadow\nIndependent corner radii", 17);
	NVSurface *glow = surface(Vector2(430, 504), "res://addons/night_voyageExt/presets/glow.tres");
	add_label(glow, Vector2(24, 24), Vector2(252, 34), "Signal / active", 24);
	add_label(glow, Vector2(24, 72), Vector2(252, 54), "SDF glow + border\nOne surface draw", 17);

	add_label(this, Vector2(810, 446), Vector2(400, 30), "03   ALPHA-CONTOUR SHADOW", 16, Color(0.5, 0.7, 0.78));
	alpha_shadow = memnew(NVAlphaShadow);
	alpha_shadow->set_position(Vector2(848, 492));
	alpha_shadow->set_size(Vector2(160, 160));
	alpha_shadow->set_texture(contour_texture);
	alpha_shadow->set_blur_radius(9);
	add_child(alpha_shadow);
	add_label(this, Vector2(1030, 520), Vector2(220, 100), "A real transparent hole.\nTwo cached GPU passes.\nMove without reblurring.", 17);

	// 参数控件仅通过信号改动效果，演示也不需要 _process 轮询滑块值。
	add_label(this, Vector2(48, 708), Vector2(160, 30), "Backdrop blur", 17);
	HSlider *blur = memnew(HSlider);
	blur->set_position(Vector2(214, 714)); blur->set_size(Vector2(250, 24));
	blur->set_min(0); blur->set_max(48); blur->set_step(0.5); blur->set_value(16);
	blur->connect("value_changed", Callable(this, "_blur_changed")); add_child(blur);
	blur_value = add_label(this, Vector2(476, 708), Vector2(70, 30), "16 px", 17);
	CheckBox *quality = memnew(CheckBox);
	quality->set_position(Vector2(562, 704)); quality->set_text("5-tap glass");
	quality->connect("toggled", Callable(this, "_quality_toggled")); add_child(quality);
	add_label(this, Vector2(810, 708), Vector2(140, 30), "Alpha softness", 17);
	HSlider *softness = memnew(HSlider);
	softness->set_position(Vector2(962, 714)); softness->set_size(Vector2(250, 24));
	softness->set_min(0); softness->set_max(36); softness->set_step(0.5); softness->set_value(9);
	softness->connect("value_changed", Callable(this, "_shadow_changed")); add_child(softness);
	add_label(this, Vector2(48, 760), Vector2(1184, 24), "night_voyageExt  /  GL Compatibility  /  separate example scene", 14, Color(0.45, 0.56, 0.64));
}

void NVEffectsDemo::on_blur_changed(double p_value) {
	glass->set_blur_radius(p_value);
	blur_value->set_text(String::num(p_value, 1) + " px");
}
void NVEffectsDemo::on_quality_toggled(bool p_enabled) { glass->set_high_quality(p_enabled); }
void NVEffectsDemo::on_shadow_changed(double p_value) { alpha_shadow->set_blur_radius(p_value); }

void NVEffectsDemo::_draw() {
	if (Engine::get_singleton()->is_editor_hint()) { return; }
	if (validation) {
		draw_rect(Rect2(0, 0, 768, 560), Color(0.22, 0.22, 0.22));
		for (int x = 16; x < 752; x += 4) {
			const float value = ((x / 4) % 2 == 0) ? 0.85f : 0.25f;
			draw_rect(Rect2(x, 16, 4, 208), Color(value, value, value));
		}
		return;
	}
	draw_rect(Rect2(0, 0, 1280, 800), Color(0.055, 0.075, 0.10));
	draw_rect(Rect2(48, 120, 1184, 296), Color(0.1, 0.19, 0.25));
	// 高频栅格与彩色带是故意保留的对照：玻璃内应模糊，文字与原图应保持清晰。
	for (int x = 48; x < 1232; x += 8) {
		draw_rect(Rect2(x, 120, 3, 296), Color(0.38, 0.66, 0.67, 0.45));
	}
	draw_circle(Vector2(246, 236), 112, Color(0.75, 0.28, 0.19));
	draw_circle(Vector2(430, 276), 94, Color(0.15, 0.63, 0.56));
	draw_line(Vector2(60, 402), Vector2(776, 136), Color(0.95, 0.7, 0.26), 16, true);
	draw_line(Vector2(74, 380), Vector2(790, 114), Color(0.95, 0.7, 0.26, 0.7), 3, true);
}

void NVEffectsDemo::build_validation() {
	glass = memnew(NVGlassPanel);
	glass->set_position(Vector2(220, 32)); glass->set_size(Vector2(208, 160));
	Ref<NVSurfaceStyle> style; style.instantiate();
	style->set_fill_color(Color(0, 0, 0, 0)); style->set_shadow_color(Color(0, 0, 0, 0));
	style->set_border_width(0); glass->set_surface_style(style);
	glass->set_saturation(1); add_child(glass);
	second_glass = memnew(NVGlassPanel);
	second_glass->set_position(Vector2(500, 32)); second_glass->set_size(Vector2(208, 160));
	Ref<NVSurfaceStyle> second_style; second_style.instantiate();
	second_style->set_fill_color(Color(0, 0, 0, 0)); second_style->set_shadow_color(Color(0, 0, 0, 0));
	second_style->set_border_width(0); second_glass->set_surface_style(second_style);
	second_glass->set_saturation(1); add_child(second_glass);
	ColorRect *sharp = memnew(ColorRect);
	sharp->set_position(Vector2(32, 58)); sharp->set_size(Vector2(124, 4));
	sharp->set_color(Color(1, 1, 1)); glass->add_child(sharp);
	add_label(glass, Vector2(24, 100), Vector2(180, 36), "SHARP TEXT", 20);

	NVSurface *shadow = memnew(NVSurface);
	shadow->set_position(Vector2(60, 300)); shadow->set_size(Vector2(150, 100));
	Ref<NVSurfaceStyle> shadow_style; shadow_style.instantiate();
	shadow_style->set_corner_radii(Vector4(20, 20, 20, 20));
	shadow_style->set_fill_color(Color(0.5, 0.5, 0.5)); shadow_style->set_border_width(0);
	shadow_style->set_shadow_color(Color(0, 0, 0, 0.8));
	shadow_style->set_shadow_softness(8); shadow->set_surface_style(shadow_style); add_child(shadow);
	NVSurface *glow = memnew(NVSurface);
	glow->set_position(Vector2(300, 300)); glow->set_size(Vector2(150, 100));
	Ref<NVSurfaceStyle> glow_style; glow_style.instantiate();
	glow_style->set_fill_color(Color(0.1, 0.1, 0.1)); glow_style->set_border_width(0);
	glow_style->set_shadow_color(Color(0, 0, 0, 0));
	glow_style->set_glow_color(Color(0.1, 0.8, 1, 1)); glow_style->set_glow_spread(4);
	glow->set_surface_style(glow_style); add_child(glow);
	alpha_shadow = memnew(NVAlphaShadow);
	alpha_shadow->set_position(Vector2(540, 300)); alpha_shadow->set_size(Vector2(160, 160));
	alpha_shadow->set_texture(contour_texture); alpha_shadow->set_show_source(false);
	alpha_shadow->set_blur_radius(6); alpha_shadow->set_shadow_offset(Vector2(0, 16));
	add_child(alpha_shadow);
	add_label(this, Vector2(24, 240), Vector2(720, 32), "SDF SHADOW                  SDF GLOW                  ALPHA SHADOW", 18);
	add_label(this, Vector2(24, 500), Vector2(720, 36), "GPU validation: screen blur / edge falloff / transparent hole / cache lifecycle", 16);
}

void NVEffectsDemo::check(bool p_condition, const String &p_description) {
	UtilityFunctions::print((p_condition ? "PASS: " : "FAIL: ") + p_description);
	if (!p_condition) { ++failures; }
}

void NVEffectsDemo::validate_pixels() {
	// 有意在测试中同步读回 GPU，验证实际后端产出的画面，而不是只检查参数值。
	Ref<Image> image = get_viewport()->get_texture()->get_image();
	check(image.is_valid() && image->get_width() == 768, "GPU viewport readback available");
	if (image.is_null() || image->get_width() != 768) { return; }
	const double clear_contrast = std::abs(image->get_pixel(40, 70).r - image->get_pixel(44, 70).r);
	const double glass_contrast = std::abs(image->get_pixel(240, 70).r - image->get_pixel(244, 70).r);
	UtilityFunctions::print("Pixel contrast clear=", clear_contrast, " glass=", glass_contrast);
	check(clear_contrast > 0.4 && glass_contrast < clear_contrast * 0.3, "glass reduces backdrop contrast");
	// 低对比度也可能来自错误的黑色屏幕复制，因此同时验证多处亮度。
	bool valid_brightness = true;
	for (int y : { 64, 80, 160, 180 }) {
		for (int x : { 244, 280, 350, 400 }) {
			const double value = image->get_pixel(x, y).r;
			valid_brightness &= value > 0.42 && value < 0.68;
		}
	}
	check(valid_brightness, "glass copy covers the entire translated panel without black regions");
	check(image->get_pixel(280, 91).r > 0.95 && image->get_pixel(280, 97).r < 0.8,
		"foreground child stays sharp");
	check(image->get_pixel(130, 418).r < 0.20, "soft outer shadow extends beyond layout rect");
	check(std::abs(image->get_pixel(62, 302).r - 0.22) < 0.025, "rounded corner cuts away surface fill");
	check(image->get_pixel(375, 410).b > 0.3, "glow extends beyond layout rect");
	const double hole = image->get_pixel(620, 396).r;
	const double ring = image->get_pixel(620, 340).r;
	UtilityFunctions::print("Alpha hole=", hole, " ring=", ring);
	check(std::abs(hole - 0.22) < 0.025 && ring < hole - 0.05, "alpha shadow preserves transparent hole");
	check(std::abs(image->get_pixel(720, 396).r - 0.22) < 0.025, "alpha cache has transparent padded borders");
}

void NVEffectsDemo::save_capture(const String &p_name) {
	DirAccess::make_dir_recursive_absolute("res://artifacts");
	Ref<Image> image = get_viewport()->get_texture()->get_image();
	const Error result = image.is_valid() ? image->save_png("res://artifacts/" + p_name) : ERR_UNAVAILABLE;
	if (validation) { check(result == OK, "save validation screenshot"); }
	UtilityFunctions::print("NV capture: res://artifacts/", p_name, " result=", int(result));
}

void NVEffectsDemo::_process(double p_delta) {
	(void)p_delta;
	++frame;
	if (capture && !validation && frame == 20) {
		save_capture("ui_effects_demo.png"); get_tree()->quit(); return;
	}
	if (!validation) { return; }
	auto read_glass_center = [](NVGlassPanel *p_glass) {
		Ref<Image> image = p_glass->get_viewport()->get_texture()->get_image();
		const Vector2 pixel = (p_glass->get_viewport_transform() * p_glass->get_global_transform()).xform(Vector2(70, 40));
		return image->get_pixel(int(pixel.x), int(pixel.y));
	};
	if (frame == 20) {
		validate_pixels(); save_capture("ui_effects_validation.png");
		initial_build_count = alpha_shadow->get_cache_build_count();
		check(initial_build_count == 1 && !alpha_shadow->is_cache_pending(), "initial cache built once and completed");
		alpha_shadow->set_position(Vector2(550, 300));
		alpha_shadow->set_rotation(0.1);
		alpha_shadow->set_shadow_color(Color(0.1, 0.2, 0.3, 0.8));
		alpha_shadow->set_shadow_offset(Vector2(4, 20));
		alpha_shadow->set_show_source(true);
		glass->set_position(Vector2(240, 34)); glass->set_scale(Vector2(0.85, 0.85));
		glass->set_rotation(0.08); glass->set_high_quality(true);
	} else if (frame == 30) {
		check(std::abs(read_glass_center(glass).r - 0.55) < 0.1, "translated/rotated/scaled glass keeps valid backdrop");
		get_window()->set_size(Vector2i(960, 700));
		check(alpha_shadow->get_cache_build_count() == initial_build_count, "transform/color/offset/source toggle reuse cache");
		// 同一帧修改三个缓存参数必须合并成一次构建。
		alpha_shadow->set_blur_radius(10); alpha_shadow->set_cache_scale(0.75);
		alpha_shadow->set_size(Vector2(164, 164));
	} else if (frame == 42) {
		check(std::abs(read_glass_center(glass).r - 0.55) < 0.1, "glass copy follows viewport content scaling");
		check(alpha_shadow->get_cache_build_count() == initial_build_count + 1 && !alpha_shadow->is_cache_pending(),
			"size/radius/quality changes coalesce into one rebuild");
		contour_texture->emit_changed();
	} else if (frame == 54) {
		check(alpha_shadow->get_cache_build_count() == initial_build_count + 2 && !alpha_shadow->is_cache_pending(),
			"texture changed signal invalidates cache");
		alpha_shadow->set_texture(Ref<Texture2D>());
		alpha_shadow->set_size(Vector2(0, 0));
		// 先绘制红色玻璃，再捕获它上面的第二层；可区分本地快照与旧的共享快照。
		second_glass->get_surface_style()->set_fill_color(Color(0.8, 0, 0, 1));
		layered_glass = memnew(NVGlassPanel);
		layered_glass->set_position(Vector2(550, 64)); layered_glass->set_size(Vector2(120, 100));
		Ref<NVSurfaceStyle> overlay_style; overlay_style.instantiate();
		overlay_style->set_fill_color(Color(0, 0, 0, 0)); overlay_style->set_shadow_color(Color(0, 0, 0, 0));
		overlay_style->set_border_width(0); layered_glass->set_surface_style(overlay_style);
		layered_glass->set_saturation(1); add_child(layered_glass);
	} else if (frame == 60) {
		const Color overlapped = read_glass_center(layered_glass);
		check(overlapped.r > 0.7 && overlapped.g < 0.1, "overlapping glass captures the earlier panel");
		// 同层批量玻璃模式：统一在它们之前复制一次完整 Viewport。
		BackBufferCopy *shared_capture = memnew(BackBufferCopy);
		shared_capture->set_copy_mode(BackBufferCopy::COPY_MODE_VIEWPORT);
		add_child(shared_capture); move_child(shared_capture, 0);
		glass->set_local_capture(false); second_glass->set_local_capture(false); layered_glass->set_local_capture(false);
		second_glass->get_surface_style()->set_fill_color(Color(0, 0, 0, 0));
		check(!alpha_shadow->is_cache_pending(), "null texture and zero size cancel rendering");
		alpha_shadow->set_texture(contour_texture); alpha_shadow->set_size(Vector2(160, 160));
		alpha_shadow->set_blur_radius(0);
	} else if (frame == 72) {
		check(std::abs(read_glass_center(glass).r - 0.55) < 0.1 &&
			std::abs(read_glass_center(second_glass).r - 0.55) < 0.1 &&
			std::abs(read_glass_center(layered_glass).r - 0.55) < 0.1, "multiple glass panels reuse one shared viewport capture");
		check(alpha_shadow->get_cache_build_count() == initial_build_count + 3 && !alpha_shadow->is_cache_pending(),
			"cache recovers after empty state and accepts zero blur");
		// 大逻辑控件与大模糊半径都不应突破用户指定的 GPU 缓存上限。
		alpha_shadow->set_max_cache_size(64); alpha_shadow->set_blur_radius(96);
		alpha_shadow->set_size(Vector2(2000, 1400));
	} else if (frame == 84) {
		const Vector2i cache_size = alpha_shadow->get_cache_size_pixels();
		check(cache_size.x <= 64 && cache_size.y <= 64 && cache_size.x >= 2 && cache_size.y >= 2 &&
			!alpha_shadow->is_cache_pending(), "large radius and large control obey GPU cache size limit");
		// 控件从树上暂时移除再加入时，资源与帧回调不能留在半完成状态。
		remove_child(alpha_shadow); add_child(alpha_shadow);
	} else if (frame == 96) {
		check(alpha_shadow->get_cache_build_count() == initial_build_count + 5 && !alpha_shadow->is_cache_pending(),
			"cache rebuilds correctly after leaving and reentering scene tree");
		UtilityFunctions::print("NV_SELF_TEST failures=", failures);
		get_tree()->quit(failures == 0 ? 0 : 1);
	}
}

} // namespace godot
