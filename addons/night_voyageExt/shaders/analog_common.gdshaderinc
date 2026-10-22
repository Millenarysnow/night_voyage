// 模拟显示参数在 CRT 全屏层和单独视频画面之间共享，避免两套参数语义漂移。
uniform float grade_brightness = 1.0;
uniform float grade_contrast = 1.0;
uniform float grade_saturation = 1.0;
uniform float scanline_strength = 0.0;
uniform float scanline_period = 3.0;
uniform float grain_strength = 0.0;
uniform float grain_scale = 1.0;
uniform float grain_speed = 12.0;
uniform float vignette_strength = 0.0;
uniform vec4 reflection_color : source_color = vec4(0.8, 0.9, 1.0, 0.12);
uniform vec2 reflection_center = vec2(0.22, 0.0);
uniform vec2 reflection_radius = vec2(0.62, 0.46);
uniform float rolling_strength = 0.0;
uniform float rolling_speed = 0.11;
uniform float rolling_width = 0.035;
uniform float flicker_strength = 0.0;
uniform float flicker_frequency = 2.0;
uniform bool motion_enabled = true;
uniform bool overlay_blend = false;

float nv_noise_hash(vec2 p) {
	return fract(sin(dot(p, vec2(127.1, 311.7))) * 43758.5453123);
}

vec3 nv_apply_analog(vec3 input_color, vec2 uv, vec2 pixel_size) {
	// 保持分层单通道处理顺序：基色调节在前，显示器缺陷叠加在后。
	float luma = dot(input_color, vec3(0.2126, 0.7152, 0.0722));
	vec3 color = mix(vec3(luma), input_color, grade_saturation);
	color = (color - vec3(0.5)) * grade_contrast + vec3(0.5);
	color *= grade_brightness;

	float activity = motion_enabled ? 1.0 : 0.0;
	if (flicker_strength > 0.0 && activity > 0.0) {
		color *= 1.0 + sin(TIME * flicker_frequency * 6.2831853) * flicker_strength;
	}
	if (grain_strength > 0.0) {
		// 网格噪点按信号帧编号跳变；暂停动态效果后 frame_index 固定为 0。
		float frame_index = floor(TIME * grain_speed * activity);
		float grain = nv_noise_hash(floor(uv / max(pixel_size, vec2(0.00001)) * grain_scale) + vec2(frame_index, frame_index * 0.37)) - 0.5;
		color += vec3(grain * grain_strength);
	}
	if (scanline_strength > 0.0) {
		float line = 0.5 + 0.5 * sin(uv.y / max(pixel_size.y * scanline_period, 0.00001) * 6.2831853);
		if (overlay_blend) {
			vec3 overlay_color = mix(vec3(0.0), vec3(1.0), line);
			vec3 overlay_result = mix(2.0 * color * overlay_color,
				1.0 - 2.0 * (1.0 - color) * (1.0 - overlay_color), step(vec3(0.5), color));
			color = mix(color, overlay_result, scanline_strength);
		} else {
			color *= 1.0 - scanline_strength * (1.0 - line);
		}
	}
	if (vignette_strength > 0.0) {
		float edge = length((uv - vec2(0.5)) * vec2(1.0, pixel_size.x / max(pixel_size.y, 0.00001)));
		color *= 1.0 - smoothstep(0.28, 0.78, edge) * vignette_strength;
	}
	if (reflection_color.a > 0.0) {
		vec2 reflection_pos = (uv - reflection_center) / max(reflection_radius, vec2(0.001));
		float reflection = (1.0 - smoothstep(0.45, 1.0, length(reflection_pos))) * reflection_color.a;
		color = mix(color, reflection_color.rgb, reflection);
	}
	if (rolling_strength > 0.0 && activity > 0.0) {
		// 对周期相位取最短距离，亮带平滑地从屏幕底部循环回顶部。
		float phase = fract(TIME * rolling_speed);
		float separation = abs(uv.y - phase);
		separation = min(separation, 1.0 - separation);
		float band = 1.0 - smoothstep(rolling_width * 0.5, rolling_width, separation);
		color += vec3(band * rolling_strength);
	}
	return color;
}
