shader_type canvas_item;

// 锁屏时网页仍叠加全局玻璃反光与扫描线；本 Shader 仅复现这层光学叠加。
void fragment() {
	vec2 sky_uv = (UV - vec2(0.22, -0.02)) / vec2(0.62, 0.46);
	float sky = (1.0 - smoothstep(0.45, 1.0, length(sky_uv))) * 0.075;
	vec2 return_uv = (UV - vec2(0.84, 1.02)) / vec2(0.34, 0.26);
	float return_light = (1.0 - smoothstep(0.35, 1.0, length(return_uv))) * 0.05;
	float top_glint = (1.0 - smoothstep(0.0, 0.012, UV.y)) * 0.07;
	float scanline = 1.0 - step(1.0, mod(FRAGCOORD.y, 3.0));
	float breath = 0.5 + 0.5 * sin(TIME * 1.2083);
	float scan_alpha = scanline * mix(0.103, 0.127, breath);
	float reflection_alpha = sky + return_light + top_glint;
	float alpha = reflection_alpha + scan_alpha * (1.0 - reflection_alpha);
	vec3 reflection_color = vec3(0.72, 0.84, 1.0);
	vec3 color = reflection_color * (reflection_alpha / max(alpha, 0.0001));
	COLOR = vec4(color, alpha);
}
