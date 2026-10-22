shader_type canvas_item;
render_mode unshaded;

// 两次一维、固定 25 次读取的高斯卷积。只处理 Alpha，不让源图片的 RGB 渗入投影。
// 该 Shader 只在缓存失效时运行；稳定帧只显示缓存纹理。
uniform sampler2D source_mask : repeat_disable, filter_linear;
uniform vec2 canvas_size = vec2(256.0);
uniform vec2 content_origin = vec2(32.0);
uniform vec2 content_size = vec2(192.0);
uniform vec2 axis = vec2(1.0, 0.0);
uniform float sigma = 8.0;
uniform bool source_is_image = true;

float alpha_at(vec2 position_px) {
	vec2 uv = source_is_image
		? (position_px - content_origin) / max(content_size, vec2(1.0))
		: position_px / canvas_size;
	// repeat_disable 会把边缘像素向外拉伸，因此明确把范围以外处理为透明。
	float inside = step(0.0, uv.x) * step(0.0, uv.y) * step(uv.x, 1.0) * step(uv.y, 1.0);
	return texture(source_mask, clamp(uv, vec2(0.0), vec2(1.0))).a * inside;
}

void fragment() {
	vec2 p = UV * canvas_size;
	float alpha = 0.0;
	float sum_weight = 0.0;
	if (sigma < 0.01) {
		// 硬轮廓只需一次读取，避免 25 次完全相同的采样。
		alpha = alpha_at(p);
		sum_weight = 1.0;
	} else {
		// 取样覆盖 ±3σ；低分辨率缓存让大阴影的成本与原图分辨率脱钩。
		for (int i = -12; i <= 12; i++) {
			float t = float(i) * 0.25;
			float weight = exp(-0.5 * t * t);
			alpha += alpha_at(p + axis * (t * sigma)) * weight;
			sum_weight += weight;
		}
	}
	COLOR = vec4(1.0, 1.0, 1.0, alpha / sum_weight);
}
