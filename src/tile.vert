#version 460

layout(push_constant) uniform table_t {
	vec2 window_size;
	vec2 center;
	vec2 size;
} push;

layout(location = 0) out vec2 coord_snorm;

void main() {
	uint i = gl_VertexIndex % (3 * 2);

	vec2 verticies[6] = vec2[](
		vec2(-1.0,  1.0),
		vec2( 1.0,  1.0),
		vec2(-1.0, -1.0),

		vec2( 1.0, -1.0),
		vec2(-1.0, -1.0)
		vec2( 1.0,  1.0),
	);

	coord_snorm = verticies[i];

	gl_Position = vec4(
		(center + size / 2.0 * verticies[i]) / window_size,
		0.0,
		1.0
	);
}