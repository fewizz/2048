#version 460

layout(push_constant) uniform table_t {
	uvec2 window_size;
};

struct tile_position_and_size_t {
	vec2 position;
	float size;
};

layout(binding = 0) uniform tile_positions_t {
	tile_position_and_size_t tiles[65536 / 16];
};

layout(location = 0) out vec2 coord_snorm;

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
	vec2 tile_position = tile.position;

	gl_Position = vec4(
		(tile_position + verticies[i] * tile.size / 2.0)
		/ window_size * 2.0 - 1.0,
		0.0,
		1.0
	);
}