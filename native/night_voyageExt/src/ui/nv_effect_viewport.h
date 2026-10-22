#pragma once

#include <godot_cpp/classes/control.hpp>
#include <godot_cpp/classes/sub_viewport.hpp>
#include <godot_cpp/classes/sub_viewport_container.hpp>

namespace godot {

/**
 * 为需要对一组 Control 做 GPU 合成的效果提供标准 SubViewport 容器。
 * 子 UI 仍保留 Godot 的焦点、键盘、布局和输入语义；容器负责按尺寸伸缩。
 */
class NVEffectViewport : public SubViewportContainer {
	GDCLASS(NVEffectViewport, SubViewportContainer)

	SubViewport *content_viewport = nullptr;
	Control *content_root = nullptr;

	void ensure_content_viewport();

protected:
	static void _bind_methods();
	void _notification(int p_what);

public:
	NVEffectViewport();
	SubViewport *get_content_viewport();
	Control *get_content_root();
};

} // namespace godot
