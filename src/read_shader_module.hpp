#pragma once

#include "./read_file.hpp"
#include <vk.hpp>

inline handle<vk::shader_module> read_shader_module(
	handle<vk::instance> instance,
	handle<vk::device> device,
	c_string<char> path
) {
	posix::memory<uint8> data = read_file(path);

	handle<vk::shader_module> tile_frag_shader_module =
		vk::create_shader_module(
			instance, device,
			vk::code{ (uint32*) data.iterator() },
			vk::code_size{ data.size() }
		);

	return tile_frag_shader_module;
}