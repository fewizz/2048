#pragma once

#include <glfw/instance.hpp>
#include <glfw/window.hpp>
#include <print/print.hpp>

inline static glfw::instance glfw_instance{};
inline static body<glfw::window> window{};

inline void init_glfw_window() {
	glfw_instance.window_hint(glfw::client_api, glfw::no_api);
	window = glfw_instance.create_window(
		glfw::width { 640 }, glfw::height { 480 }, glfw::title { u8"2048"s }
	);
	print::out("window is created\n");
}