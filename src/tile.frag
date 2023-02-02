#version 460

layout(location = 0) in vec2 coord_snorm;

layout(location = 0) out vec4 out_color;

void main() {
	out_color = vec4(0.0, 0.0, 1.0 - dot(coord_snorm, coord_snorm), 1.0);
}