#include <posix/unhandled.hpp>
#include <posix/abort.hpp>
#include <vk/__internal/unexpected_handler.hpp>
#include <glfw/__internal/unexpected_handler.hpp>

namespace vk {
	[[ noreturn ]] inline void unexpected_handler() {
		posix::abort();
	}

	[[ noreturn ]] inline void unexpected_handler(vk::result) {
		posix::abort();
	}
}

[[noreturn]] inline void posix::unhandled_t::operator () () const {
	posix::abort();
}
[[noreturn]] inline void posix::unhandled_t::operator () (posix::error) const {
	posix::abort();
}

namespace glfw {

	[[ noreturn ]]
	inline void unexpected_handler() {
		posix::abort();
	}

	[[ noreturn ]]
	inline void unexpected_handler(glfw::error) {
		posix::abort();
	}

} // glfw

#if __MINGW32__
#include <win/unhandled.hpp>
[[ noreturn ]] inline void win::unhandled_t::operator () () const {
	posix::abort();
}

[[ noreturn ]] inline void win::unhandled_t::operator () (win::error) const {
	posix::abort();
}
#endif