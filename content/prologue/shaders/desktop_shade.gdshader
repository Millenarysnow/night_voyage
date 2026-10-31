shader_type canvas_item;

// 桌面渐层只负责原型中的整体压暗；弧面、扫描线与反光由 CRT 插件视口处理。
void fragment() {
	vec2 radial_uv = (UV - vec2(0.5, 0.1)) / vec2(0.6, 0.45);
	float radial_edge = smoothstep(0.55, 1.2, length(radial_uv));
	float radial_alpha = mix(0.30, 0.92, radial_edge);
	float vertical_alpha = UV.y < 0.4
		? mix(0.55, 0.15, smoothstep(0.0, 0.4, UV.y))
		: mix(0.15, 0.80, smoothstep(0.4, 1.0, UV.y));
	float alpha = radial_alpha + vertical_alpha * (1.0 - radial_alpha);
	COLOR = vec4(vec3(0.018, 0.024, 0.038), alpha);
}
