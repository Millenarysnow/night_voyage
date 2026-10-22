shader_type canvas_item;
render_mode unshaded;

#include "analog_common.gdshaderinc"

uniform sampler2D footage : source_color, repeat_disable, filter_linear;
uniform vec2 footage_size = vec2(640.0, 360.0);

void fragment() {
	// 监控只采样自己的源纹理；Viewport 以外的游戏 UI 不会被这层误处理。
	vec4 frame_color = texture(footage, UV);
	vec3 effected = nv_apply_analog(frame_color.rgb, UV, 1.0 / max(footage_size, vec2(1.0)));
	COLOR = vec4(effected, frame_color.a) * COLOR;
}
