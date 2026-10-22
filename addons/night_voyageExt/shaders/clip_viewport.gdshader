shader_type canvas_item;
render_mode unshaded;

#include "sdf_common.gdshaderinc"

uniform vec2 viewport_size = vec2(320.0, 180.0);
uniform int shape_type = 0;
uniform vec4 corner_radii = vec4(16.0);
uniform sampler2D alpha_mask : source_color, repeat_disable, filter_linear;
uniform float alpha_threshold = 0.5;

varying vec4 item_modulation;

void vertex() {
	// SubViewportContainer 的片元 COLOR 已含底图，先保存独立的节点调制色以免重复乘贴图。
	item_modulation = COLOR;
}

void fragment() {
	vec4 content = texture(TEXTURE, UV);
	float mask = 1.0;
	if (shape_type == 0) {
		// 半径顺序与 NVHitShape 的 CPU 距离判断一致，边缘由共享 SDF 做抗锯齿。
		float distance_value = nv_round_rect(UV * viewport_size, viewport_size, corner_radii);
		mask = nv_coverage(distance_value);
	} else {
		// Alpha 模式采样与 CPU 缓存同源的限尺寸遮罩，避免显示/点击分辨率不同。
		float mask_alpha = texture(alpha_mask, UV).a;
		mask = smoothstep(alpha_threshold - 0.02, alpha_threshold + 0.02, mask_alpha);
	}
	COLOR = content * item_modulation * vec4(1.0, 1.0, 1.0, mask);
}
