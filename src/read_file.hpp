#pragma once

#include <posix/memory.hpp>
#include <posix/io.hpp>

posix::memory_for_range_of<uint8> read_file(any_c_string auto path) {
	body<posix::file> file = posix::open_file(
		path,
		posix::file_access_modes {
			posix::file_access_mode::read,
			posix::file_access_mode::binary
		}
	);

	nuint size = file->get_size();

	posix::memory_for_range_of<uint8> mem
		= posix::allocate_memory_for<uint8>(size);

	auto read = file->read_to(mem);
	if(read != size) { posix::abort(); }
	return mem;
}