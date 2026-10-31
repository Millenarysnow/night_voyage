shader_type canvas_item;

// 锁屏盖在 CRT 弧面之外，使用网页登录遮罩的中心透明、边缘渐暗分布。
void fragment() {
	vec2 radial_uv = (UV - vec2(0.5, 0.2)) / vec2(0.72, 0.9);
	float edge = smoothstep(0.0, 1.1, length(radial_uv));
	float alpha = mix(0.42, 0.88, edge);
	COLOR = vec4(vec3(0.018, 0.024, 0.038), alpha);
}
