shader_type canvas_item;
render_mode unshaded;

uniform sampler2D blurred_mask : repeat_disable, filter_linear;
uniform sampler2D source_image : source_color, repeat_disable, filter_linear;
uniform vec2 content_size = vec2(192.0);
uniform vec2 cache_size = vec2(256.0);
uniform float cache_padding = 32.0;
uniform float draw_padding = 64.0;
uniform vec2 shadow_offset = vec2(0.0, 14.0);
uniform vec4 shadow_color : source_color = vec4(0.0, 0.0, 0.0, 0.65);
uniform bool show_source = true;

void fragment() {
	vec4 modulation = COLOR;
	vec2 p = UV * (content_size + vec2(draw_padding * 2.0)) - vec2(draw_padding);
	vec2 mask_uv = (p - shadow_offset + vec2(cache_padding)) / cache_size;
	float valid_mask = step(0.0, mask_uv.x) * step(0.0, mask_uv.y)
		* step(mask_uv.x, 1.0) * step(mask_uv.y, 1.0);
	float shadow_alpha = texture(blurred_mask, clamp(mask_uv, vec2(0.0), vec2(1.0))).a * valid_mask * shadow_color.a;
	vec4 result = vec4(shadow_color.rgb * shadow_alpha, shadow_alpha);
	if (show_source) {
		vec2 uv = p / max(content_size, vec2(1.0));
		float inside = step(0.0, uv.x) * step(0.0, uv.y) * step(uv.x, 1.0) * step(uv.y, 1.0);
		vec4 source = texture(source_image, clamp(uv, vec2(0.0), vec2(1.0)));
		source.a *= inside;
		result = vec4(source.rgb * source.a, source.a) + result * (1.0 - source.a);
	}
	COLOR = vec4(result.rgb / max(result.a, 0.00001), result.a) * modulation;
}
