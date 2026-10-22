shader_type canvas_item;
render_mode unshaded;

#include "sdf_common.gdshaderinc"
#include "surface_uniforms.gdshaderinc"

void fragment() {
	vec4 modulation = COLOR;
	vec2 p = UV * (surface_size + vec2(draw_padding * 2.0)) - vec2(draw_padding);
	float d = nv_round_rect(p, surface_size, corner_radii);
	float shape = nv_coverage(d);
	vec4 result = nv_outer_layers(p, d, shape);
	result = nv_over(result, fill_color, shape);
	result = nv_inner_layers(result, p, d, shape);
	COLOR = nv_straight(result) * modulation;
}
