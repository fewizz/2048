#version 460

layout(push_constant) uniform table_t {
	uvec2 window_size;
};

struct tile_position_and_size_t {
	vec3 position;
	float size;
	uint number;
};

layout(binding = 0) uniform tile_positions_t {
	tile_position_and_size_t tiles[65536 / 16];
};

layout(location = 0) out vec2 coord_snorm;
layout(location = 1) flat out uint value;

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

	coord_snorm = verticies[i];

	tile_position_and_size_t tile = tiles[gl_VertexIndex / (3 * 2)];
	value = tile.number;

	gl_Position = vec4(
		(tile.position.xy + verticies[i] * tile.size / 2.0)
		/ window_size * 2.0 - 1.0,
		tile.position.z,
		1.0
	);
}