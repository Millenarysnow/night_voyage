#pragma once

#include "../ui/nv_alpha_shadow.h"
#include "../ui/nv_glass_panel.h"
#include "../ui/nv_clip_viewport.h"
#include "../ui/nv_screen_effect_viewport.h"
#include "../ui/nv_footage_effect.h"
#include <godot_cpp/classes/button.hpp>
#include <godot_cpp/classes/image_texture.hpp>
#include <godot_cpp/classes/label.hpp>

namespace godot {

/**
 * 独立演示场景的原生控制器，不依赖游戏业务，也不更改项目的主场景。
 * 普通演示仅由信号更新；--nv-self-test 才启用逐帧验证与 GPU 截图读回。
 * 注意：截图读回只在验证代码中使用，三个运行时组件均不读取 GPU 像素。
 */
class NVEffectsDemo : public Control {
	GDCLASS(NVEffectsDemo, Control)
	NVGlassPanel *glass = nullptr;
	NVGlassPanel *second_glass = nullptr;
	NVGlassPanel *layered_glass = nullptr;
	NVAlphaShadow *alpha_shadow = nullptr;
	NVScreenEffectViewport *screen_effect = nullptr;
	NVClipViewport *clip_viewport = nullptr;
	NVClipViewport *alpha_clip_viewport = nullptr;
	NVFootageEffect *footage_effect = nullptr;
	Button *screen_test_button = nullptr;
	Button *clip_test_button = nullptr;
	Button *alpha_test_button = nullptr;
	Label *blur_value = nullptr;
	Ref<ImageTexture> contour_texture;
	bool validation = false;
	bool capture = false;
	bool pipeline_demo = false;
	bool pipeline_validation = false;
	int frame = 0;
	int failures = 0;
	int screen_button_count = 0;
	int clip_button_count = 0;
	int alpha_button_count = 0;
	int64_t initial_build_count = 0;

	void build_demo();
	void build_validation();
	void build_pipeline_demo(bool p_validation);
	void process_pipeline_validation();
	void validate_pipeline_pixels();
	void validate_pixels();
	void check(bool p_condition, const String &p_description);
	void save_capture(const String &p_name);
	void on_blur_changed(double p_value);
	void on_quality_toggled(bool p_enabled);
	void on_shadow_changed(double p_value);
	void on_pipeline_screen_pressed();
	void on_pipeline_clip_pressed();
	void on_pipeline_alpha_pressed();
	void inject_pointer_click(Vector2 p_position);
	static Ref<ImageTexture> make_contour_texture();
	static Ref<ImageTexture> make_footage_texture();
	static Ref<ImageTexture> make_displacement_texture();
	static Ref<ImageTexture> make_ring_mask_texture();
	static Label *add_label(Node *p_parent, Vector2 p_position, Vector2 p_size,
		const char *p_text, int p_font_size = 18, Color p_color = Color(0.9, 0.94, 1));

protected:
	static void _bind_methods();

public:
	void _ready() override;
	void _draw() override;
	void _process(double p_delta) override;
};

} // namespace godot
