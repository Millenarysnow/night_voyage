// 几何尺寸由 C++ 在尺寸/样式变化时上传，不需要每帧轮询。
uniform vec2 surface_size = vec2(240.0, 140.0);
uniform float draw_padding = 0.0;
uniform int fill_mode = 0;
uniform vec4 corner_radii = vec4(16.0);
uniform vec4 fill_color : source_color = vec4(0.08, 0.10, 0.15, 1.0);
uniform sampler2D fill_gradient : source_color, repeat_disable, filter_linear;
uniform bool has_fill_gradient = false;
uniform vec2 gradient_start = vec2(0.0, 0.0);
uniform vec2 gradient_end = vec2(1.0, 1.0);
uniform vec2 gradient_center = vec2(0.5, 0.5);
uniform vec2 gradient_radius = vec2(0.5, 0.5);
uniform vec4 border_color : source_color = vec4(0.5, 0.6, 0.8, 0.3);
uniform float border_width = 1.0;
uniform vec4 shadow_color : source_color = vec4(0.0, 0.0, 0.0, 0.5);
uniform vec2 shadow_offset = vec2(0.0, 12.0);
uniform float shadow_softness = 12.0;
uniform float shadow_spread = 0.0;
uniform vec4 glow_color : source_color = vec4(1.0, 0.7, 0.2, 0.0);
uniform float glow_softness = 12.0;
uniform float glow_spread = 0.0;
uniform vec4 inner_shadow_color : source_color = vec4(0.0);
uniform vec2 inner_shadow_offset = vec2(0.0, 3.0);
uniform float inner_shadow_softness = 5.0;

vec4 nv_fill_at(vec2 p) {
	if (!has_fill_gradient || fill_mode == 0) {
		return fill_color;
	}
	vec2 uv = clamp(p / max(surface_size, vec2(1.0)), vec2(0.0), vec2(1.0));
	float amount = 0.0;
	if (fill_mode == 1) {
		vec2 direction = gradient_end - gradient_start;
		amount = dot(uv - gradient_start, direction) / max(dot(direction, direction), 0.000001);
	} else {
		amount = length((uv - gradient_center) / max(gradient_radius, vec2(0.001)));
	}
	return texture(fill_gradient, vec2(clamp(amount, 0.0, 1.0), 0.5));
}

vec4 nv_outer_layers(vec2 p, float d, float shape) {
	vec4 result = vec4(0.0);
	// Alpha 为零的效果直接跳过：纯描边/纯光晕组件不会支付其他层的计算量。
	// 在表面内部剔除外阴影，透明的玻璃不会因背后的黑色阴影整体变脏。
	if (shadow_color.a > 0.0) {
		float sd = nv_round_rect(p - shadow_offset, surface_size, corner_radii) - shadow_spread;
		result = nv_over(result, shadow_color, nv_soft_coverage(sd, shadow_softness) * (1.0 - shape));
	}
	if (glow_color.a > 0.0) {
		result = nv_over(result, glow_color, nv_soft_coverage(d - glow_spread, glow_softness) * (1.0 - shape));
	}
	return result;
}

vec4 nv_inner_layers(vec4 result, vec2 p, float d, float shape) {
	if (inner_shadow_color.a > 0.0) {
		float sd = nv_round_rect(p - inner_shadow_offset, surface_size, corner_radii);
		result = nv_over(result, inner_shadow_color, (1.0 - nv_soft_coverage(sd, inner_shadow_softness)) * shape);
	}
	if (border_width > 0.0 && border_color.a > 0.0) {
		float border_mask = max(shape - nv_coverage(d + border_width), 0.0);
		result = nv_over(result, border_color, border_mask);
	}
	return result;
}
