shader_type canvas_item;
render_mode unshaded;

#include "analog_common.gdshaderinc"

uniform vec2 viewport_size = vec2(1280.0, 720.0);
uniform float radial_strength = 0.0;
uniform vec2 warp_center = vec2(0.5, 0.5);
uniform sampler2D displacement_map : repeat_disable, filter_linear;
uniform vec2 displacement_scale_px = vec2(84.0);
uniform bool use_displacement_map = false;

varying vec4 item_modulation;

void vertex() {
	// 保存节点 modulate；片元 COLOR 对 SubViewportContainer 来说包含主纹理采样颜色。
	item_modulation = COLOR;
}

vec2 source_uv_for(vec2 display_uv) {
	// 先应用按宽高比校正的径向畸变，再叠加以输出像素计量的 RG 位移。
	float aspect = viewport_size.x / max(viewport_size.y, 1.0);
	vec2 centered = display_uv - warp_center;
	vec2 radial_metric = centered * vec2(aspect, 1.0);
	float radius_squared = dot(radial_metric, radial_metric);
	vec2 source_uv = warp_center + centered * (1.0 + radial_strength * radius_squared);
	if (use_displacement_map) {
		source_uv += (texture(displacement_map, display_uv).rg - vec2(0.5)) * displacement_scale_px / viewport_size;
	}
	return source_uv;
}

void fragment() {
	vec2 source_uv = source_uv_for(UV);
	// 超出原始画面处输出透明，且不使用 fragment early-return 以兼容 GLES。
	float valid_region = float(all(greaterThanEqual(source_uv, vec2(0.0))) &&
		all(lessThanEqual(source_uv, vec2(1.0))));
	vec4 source = texture(TEXTURE, clamp(source_uv, vec2(0.0), vec2(1.0)));
	vec3 effected = nv_apply_analog(source.rgb, UV, 1.0 / max(viewport_size, vec2(1.0)));
	COLOR = vec4(effected, source.a) * item_modulation * valid_region;
}
