// 夜航 UI：所有圆角表面共用的解析距离场。
// 单位是控件的局部像素；负值在形状内部，正值在外部。
// radii 顺序为左上、右上、右下、左下。先限制半径，避免小控件翻折。
float nv_round_rect(vec2 p, vec2 size, vec4 radii) {
	vec2 centered = p - size * 0.5;
	vec4 r = clamp(radii, vec4(0.0), vec4(min(size.x, size.y) * 0.5));
	float radius = centered.x > 0.0
		? (centered.y > 0.0 ? r.z : r.y)
		: (centered.y > 0.0 ? r.w : r.x);
	vec2 q = abs(centered) - size * 0.5 + radius;
	return length(max(q, vec2(0.0))) + min(max(q.x, q.y), 0.0) - radius;
}

// 使用导数做边缘抗锯齿，控件缩放、旋转后仍按实际屏幕像素平滑。
float nv_coverage(float distance_value) {
	float aa = max(fwidth(distance_value), 0.001);
	return 1.0 - smoothstep(-aa * 0.5, aa * 0.5, distance_value);
}

// 正态分布累积分布函数的廉价近似，避免矩形阴影做大核纹理模糊。
// softness 表示近似高斯标准差，不声称与 CSS blur-radius 一比一相等。
float nv_soft_coverage(float distance_value, float softness) {
	if (softness < 0.01) {
		return nv_coverage(distance_value);
	}
	float x = distance_value / max(softness, 0.001);
	float t = 1.0 / (1.0 + 0.2316419 * abs(x));
	float poly = t * (0.31938153 + t * (-0.35656378 + t *
		(1.78147794 + t * (-1.82125598 + t * 1.33027443))));
	float tail = 0.39894228 * exp(-0.5 * x * x) * poly;
	return x >= 0.0 ? tail : 1.0 - tail;
}

// 以下所有中间结果使用预乘 Alpha 合成，最后再转回 Godot 普通混合所需的直通颜色。
// 这样透明填充与柔和阴影重叠时不会产生黑边或额外的亮度。
vec4 nv_over(vec4 under, vec4 straight_color, float mask) {
	float alpha = clamp(straight_color.a * mask, 0.0, 1.0);
	return vec4(straight_color.rgb * alpha, alpha) + under * (1.0 - alpha);
}

vec4 nv_straight(vec4 premultiplied) {
	return vec4(premultiplied.rgb / max(premultiplied.a, 0.00001), premultiplied.a);
}
