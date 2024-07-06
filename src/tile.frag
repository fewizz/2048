#version 460

layout(location = 0) in vec2 coord_snorm;
layout(location = 1) flat in uint value;

layout(location = 0) out vec4 out_color;

void main() {
	vec3 color = vec3(
		log2(value) / 11.0 / 2.0,
		log2(value) / 11.0,
		log2(value) / 11.0 + 0.5
	);
	
	if (value == 0u) {
		color = vec3(0.1);
	}

	vec2 coord = vec2(abs(coord_snorm.x), abs(coord_snorm.y));

	float r = 0.5;

	if (coord.x < 1.0 - r || coord.y < 1.0 - r) {
		out_color = vec4(color, 1.0);
	}
	else {
		float b = float(length(coord - (1.0 - r)) <= r);
		out_color = vec4(color * b, b);
	}
}