#pragma once

#include "./read_file.hpp"

#include <png.h>
#include <posix/abort.hpp>
#include <posix/memory.hpp>
#include <c_string.hpp>

struct png_data {
	posix::memory<uint8> bytes;
	uint32 width;
	uint32 height;
};

png_data read_png(any_c_string auto path) {
	png_structp png_ptr = png_create_read_struct(
		PNG_LIBPNG_VER_STRING, nullptr, nullptr, nullptr
	);
	if(!png_ptr) {
		abort();
	}

	png_infop info_ptr = png_create_info_struct(png_ptr);
	if(!info_ptr) {
		abort();
	}

	posix::memory<uint8> file = read_file(path);

	png_image image {
		.version = PNG_IMAGE_VERSION
	};

	if(!png_image_begin_read_from_memory(
		&image,
		file.iterator(),
		file.size())
	) {
		posix::abort();
	}

	uint32 width = image.width;
	uint32 height = image.height;

	image.format = PNG_FORMAT_GRAY;

	auto size = PNG_IMAGE_SIZE(image);
	posix::memory<uint8> buffer = posix::allocate<uint8>(size);

	if(!png_image_finish_read(&image, nullptr, buffer.iterator(), 0, 0)) {
		posix::abort();
	}

	png_destroy_read_struct(&png_ptr, &info_ptr, nullptr);

	return {
		.bytes = move(buffer),
		.width = width,
		.height = height
	};
}