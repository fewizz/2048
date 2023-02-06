#version 460

layout(location = 0) out vec4 out_color;

layout(location = 0) in vec2 tex_coord_snorm;
layout(location = 1) flat in uint ascii;

layout(set = 0, binding = 1) uniform sampler2D u_digits_and_numbers;

float get_letter_or_digit_opacity(vec2 pos) {
	return texture(
		u_digits_and_numbers,
		(tex_coord_snorm * 0.5 + 0.5 + pos - vec2(0.0, 0.12)) *
		vec2(1.0 / 10.0, 1.0 / 4.0)
	).r;
}

void main() {
	float r = 0.0;

	if(ascii >= 48 && ascii <= 57) {
		r = get_letter_or_digit_opacity(vec2(ascii - 48, 0));
	}
	if(ascii >= 97 && ascii <= 122) {
		uint i = ascii - 97;
		r = get_letter_or_digit_opacity(vec2(i % 10, 1 + i / 10));
	}

	out_color = vec4(vec3(1.0), r);
}