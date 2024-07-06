#pragma once

#include <posix/memory.hpp>
#include <posix/abort.hpp>
#include <posix/io.hpp>

inline posix::memory<uint8> read_file(c_string<char> path) {
	body<posix::file> file = posix::open_file(
		path,
		posix::file_access_modes {
			posix::file_access_mode::read,
			posix::file_access_mode::binary
		}
	);

	nuint size = file->get_size();

	posix::memory<uint8> mem = posix::allocate<uint8>(size);

	auto read = file->read_to(mem);
	if (read != size) { posix::abort(); }
	return mem;
}