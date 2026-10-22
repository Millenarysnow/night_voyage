shader_type canvas_item;
render_mode unshaded;

#include "sdf_common.gdshaderinc"
#include "surface_uniforms.gdshaderinc"

// Godot 4.8 的 Compatibility 已支持屏幕高斯 mip 链；由引擎在 GPU 端生成并复用。
// 默认每片玻璃只读取一次 mip；高质量档才增加四个错位采样。
uniform sampler2D backdrop : hint_screen_texture, repeat_disable, filter_linear_mipmap;
uniform float blur_radius = 16.0;
uniform float saturation = 1.1;
uniform float brightness = 1.0;
uniform bool high_quality = false;

void fragment() {
	vec4 modulation = COLOR;
	vec2 p = UV * (surface_size + vec2(draw_padding * 2.0)) - vec2(draw_padding);
	float d = nv_round_rect(p, surface_size, corner_radii);
	float shape = nv_coverage(d);
	vec4 result = nv_outer_layers(p, d, shape);
	// 扩展绘制区域只负责阴影/光晕；表面以外不读取昂贵的屏幕纹理。
	if (shape > 0.0) {
		float lod = log2(max(blur_radius * 0.5, 1.0));
		vec3 color = textureLod(backdrop, SCREEN_UV, lod).rgb;
		if (high_quality && blur_radius > 0.0) {
			vec2 offset_uv = SCREEN_PIXEL_SIZE * blur_radius * 0.35;
			color *= 0.2;
			color += textureLod(backdrop, SCREEN_UV + offset_uv, lod).rgb * 0.2;
			color += textureLod(backdrop, SCREEN_UV - offset_uv, lod).rgb * 0.2;
			color += textureLod(backdrop, SCREEN_UV + vec2(offset_uv.x, -offset_uv.y), lod).rgb * 0.2;
			color += textureLod(backdrop, SCREEN_UV + vec2(-offset_uv.x, offset_uv.y), lod).rgb * 0.2;
		}
		float gray = dot(color, vec3(0.2126, 0.7152, 0.0722));
		color = mix(vec3(gray), color, saturation) * brightness;
		// fill_color 的 Alpha 表示玻璃染色的强度，绝不模糊其子控件上的文字。
		color = mix(color, fill_color.rgb, fill_color.a);
		result = nv_over(result, vec4(color, 1.0), shape);
	}
	result = nv_inner_layers(result, p, d, shape);
	COLOR = nv_straight(result) * modulation;
}
