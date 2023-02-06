#version 460

layout(location = 0) out vec2 tex_coord_snorm;
layout(location = 1) flat out uint ascii;

struct position_and_letter_t {
	vec3 position;
	uint letter;
	float width;
};

layout(push_constant) uniform table_t {
	uvec2 window_size;
};

layout(set = 0, binding = 0) uniform positions_and_letters_t {
	position_and_letter_t positions_and_letters[65536 / 8];
};

void main() {
	uint i = gl_VertexIndex % (3 * 2);

	vec2 verticies[6] = vec2[](
		vec2(-1.0,  1.0),
		vec2( 1.0,  1.0),
		vec2(-1.0, -1.0),

		vec2( 1.0, -1.0),
		vec2(-1.0, -1.0),
		vec2( 1.0,  1.0)
	);

	tex_coord_snorm = verticies[i];

	position_and_letter_t pos_and_letter
		= positions_and_letters[gl_VertexIndex / (3 * 2)];

	ascii = pos_and_letter.letter;
	vec2 position = pos_and_letter.position.xy;

	gl_Position = vec4(
		(position + verticies[i] * pos_and_letter.width * vec2(1.0, 2.0) / 2.0)
		/ window_size * 2.0 - 1.0,
		pos_and_letter.position.z,
		1.0
	);
}