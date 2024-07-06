#include "./glfw.hpp"

#include <vk/instance.hpp>
#include <vk/device.hpp>

#include <print/print.hpp>
#include <glfw/instance.hpp>
#include <posix/abort.hpp>


template<typename Function>
struct function_holder {
	static typename Function::prototype function_ptr;
};

template<typename Function>
typename Function::prototype function_holder<Function>::function_ptr = nullptr;

template<typename Function>
typename Function::prototype
vk::get_global_function_t<Function>::operator () () const {
	using prototype = typename Function::prototype;
	const char* name = Function::name;

	if (function_holder<Function>::function_ptr == nullptr) {
		function_holder<Function>::function_ptr = 
			(prototype) glfw_instance.get_global_proc_address(
				c_string { name }
			);
	}

	if (function_holder<Function>::function_ptr == nullptr) {
		print::err("couldn't find global function", name);
		posix::abort();
	}

	return function_holder<Function>::function_ptr;
}

template<typename Function>
typename Function::prototype
vk::get_instance_function_t<Function>::operator () (
	handle<vk::instance> instance
) const {
	using prototype = typename Function::prototype;
	const char* name = Function::name;

	if (function_holder<Function>::function_ptr == nullptr) {
		function_holder<Function>::function_ptr = 
			(prototype) glfw_instance.get_instance_proc_address(
				instance,
				c_string { name }
			);
	}

	if (function_holder<Function>::function_ptr == nullptr) {
		print::err("couldn't find instance function", name);
		posix::abort();
	}

	return function_holder<Function>::function_ptr;
}

template<typename Function>
typename Function::prototype
vk::get_device_function_t<Function>::operator () (
	handle<vk::instance> instance,
	handle<vk::device> device
) const {
	using prototype = typename Function::prototype;
	const char* name = Function::name;

	if (function_holder<Function>::function_ptr == nullptr) {
		function_holder<Function>::function_ptr = 
			(prototype) vk::get_device_proc_address(
				instance, device, c_string { name }
			);
	}

	if (function_holder<Function>::function_ptr == nullptr) {
		print::err("couldn't find device function", name);
		posix::abort();
	}

	return function_holder<Function>::function_ptr;
}