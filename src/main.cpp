#include "./handlers.hpp"
#include "./vk_functions.hpp"
#include "./glfw.hpp"
#include "./read_file.hpp"
#include "./read_png.hpp"

#include <vk.hpp>

#include <glfw/window.hpp>

#include <print/print.hpp>

#include <posix/io.hpp>
#include <posix/memory.hpp>

#include <on_scope_exit.hpp>
#include <array.hpp>
#include <ranges.hpp>
#include <number.hpp>
#include <list.hpp>

static constexpr math::vector<uint32, 2> table_size{ uint32(4), uint32(4) };

static struct push_constant_t {
	uint32 size[2];
	uint32 table_size[2] = { ::table_size[0], ::table_size[1] };
	array<array<uint32, 3>, 3> table{};
} push_constant;

extern "C" long rand();

enum class direction {
	up, down, left, right
};

/*
 default: (up)
 [0][0] [0][1] [0][2]
 [1][0] [1][1] [1][2]
 [2][0] [2][1] [2][2]
*/
template<direction Dir>
constexpr auto rotated_view(auto& table) {
	if constexpr(Dir == direction::up) {
		return table.transform_view([](auto& e) -> auto& { return e; });
	}
	else if constexpr(Dir == direction::down) {
		return table.reverse_view();
	}
	else {
		return table.transform_view([&](array<uint32, 3>& y_value) {
			nuint y_index = &y_value - table.iterator();
			return y_value.transform_view(
				[&, y_index = y_index](uint32& x_value) -> uint32& {
					nuint x_index = &x_value - y_value.iterator();
					if constexpr(Dir == direction::left) {
						return table[x_index][y_index];
					}
					else {
						return table[x_index].reverse_view()[y_index];
					}
				}
			);
		});
	}
}

void add_random_tile() {
	nuint tile_values_raw_size
		= push_constant.table.size() * push_constant.table[0].size();
	storage<uint32*> tile_values_raw[tile_values_raw_size];

	list tile_values{ span{ tile_values_raw, tile_values_raw_size } };

	for(nuint y = 0; y < push_constant.table.size(); ++y) {
		for(nuint x = 0; x < push_constant.table[y].size(); ++x) {
			uint32& value = push_constant.table[y][x];
			if(value == 0) {
				tile_values.emplace_back(&value);
			}
		}
	}

	nuint rand_index = rand() % tile_values.size();
	uint32 rand_value = (rand() % 2 + 1) * 2;
	*tile_values[rand_index] = rand_value;
}

int main() {
	add_random_tile();
	add_random_tile();

	if(!glfw_instance.is_vulkan_supported()) {
		print::err("vulkan isn't supported\n");
	}

	init_glfw_window();
		window->set_key_callback(
		+[](
			glfw::window*, glfw::key::code key, int,
			glfw::key::action action, glfw::key::modifiers
		) {
			if(action != glfw::key::action::press) return;

			auto move = []<direction Dir>() {
				auto table = rotated_view<Dir>(push_constant.table);
				bool changed = false;

				for(nuint y = 1; y < table.size(); ++y) {
					for(nuint x = 0; x < table[y].size(); ++x) {
						nuint empty_y = -1;
						for(nuint offset = 1; offset <= y; ++offset) {
							if(table[y - offset][x] == 0) {
								empty_y = y - offset;
							}
							else if(table[y - offset][x] == table[y][x]) {
								table[y - offset][x] *= 2;
								table[y][x] = 0;
								changed = true;
								break;
							}
						}
						if(empty_y != nuint(-1)) {
							table[empty_y][x] = table[y][x];
							table[y][x] = 0;
							changed = true;
						}
					}
				}

				if(changed) {
					add_random_tile();
				}
			};

			switch (key) {
				case glfw::keys::w: move.operator ()<direction::up>();    break;
				case glfw::keys::s: move.operator ()<direction::down>();  break;
				case glfw::keys::a: move.operator ()<direction::left>();  break;
				case glfw::keys::d: move.operator ()<direction::right>(); break;
			}
		}
	);

	handle<vk::instance> instance =
		ranges {
			glfw_instance.get_required_instance_extensions(),
			array{ vk::extension_name{ "VK_EXT_debug_report" } }
		}.concat_view().view_copied_elements_on_stack(
			[](span<vk::extension_name> extensions_names) {
				return vk::create_instance(
					vk::application_info {
						vk::api_version{ vk::major{ 1 }, vk::minor{ 0 } }
					},
					extensions_names,
					array{ vk::layer_name{ "VK_LAYER_KHRONOS_validation" } }
				);
			}
		);
	on_scope_exit destroy_instance = [&] {
		vk::destroy_instance(instance);
	};

	print::out("instance created\n");

	handle<vk::debug_report_callback> debug_report_callback
		= vk::create_debug_report_callback(
			instance,
			vk::debug_report_flags {
				vk::debug_report_flag::error,
				vk::debug_report_flag::warning,
				vk::debug_report_flag::performance_warning
			},
			+[](
				[[maybe_unused]] enum_flags<vk::debug_report_flag> flags,
				[[maybe_unused]] vk::debug_report_object_type objectType,
				[[maybe_unused]] uint64 object,
				[[maybe_unused]] nuint location,
				[[maybe_unused]] int32 message_code,
				[[maybe_unused]] c_string_of_unknown_size layer_prefix,
				c_string_of_unknown_size message,
				[[maybe_unused]] void* user_data
			) -> uint32 {
				print::out("[vk] ", c_string{ message }.sized(), "\n");
				return 0;
			}
		);
	on_scope_exit destroy_debug_report_callback = [&] {
		vk::destroy_debug_report_callback(instance, debug_report_callback);
	};

	handle<vk::physical_device> physical_device
		= instance->get_first_physical_device();

	print::out("physical device selected\n");

	handle<vk::surface> surface = window->create_surface(instance);
	print::out("surface created\n");
	on_scope_exit destroy_surface = [&] {
		vk::destroy_surface(instance, surface);
	};

	vk::surface_format surface_format
		= vk::get_first_physical_device_surface_format(
			instance, physical_device,
			surface
		);

	print::out("surface format selected\n");

	vk::queue_family_index queue_family_index = vk::queue_family_ignored;

	vk::view_physical_device_queue_family_properties(
		instance, physical_device,
		[&](span<vk::queue_family_properties> props_span) {
			props_span.for_each_indexed([&](auto props, uint32 index) {
				bool graphics = props.flags.get(vk::queue_flag::graphics);
				bool supports_surface =
					vk::get_physical_device_surface_support(
						instance, physical_device,
						surface,
						vk::queue_family_index{ index }
					);

				if(graphics && supports_surface) {
					queue_family_index = index;
					return loop_action::stop;
				}

				return loop_action::next;
			});
		}
	);

	print::out("queue family index selected\n");

	vk::queue_priority queue_priority = 1.0F;

	handle<vk::device> device = vk::create_device(
		instance, physical_device,
		array { vk::queue_create_info {
			queue_family_index,
			vk::queue_count{ 1 },
			vk::queue_priorities{ &queue_priority }
		}},
		array{ vk::extension_name{ "VK_KHR_swapchain" } }
	);
	on_scope_exit destroy_device = [&] {
		vk::destroy_device(instance, device);
	};

	print::out("device created\n");

	png_data digits_and_letters_image_data =
		read_png(c_string{ "digits_and_letters.png" });

	print::out("\"digits_and_letters.png\" read\n");

	handle<vk::image> digits_and_letters_image = vk::create_image(
		instance, device,
		vk::image_type::two_d,
		vk::format::r8_unorm,
		vk::extent<3> {
			digits_and_letters_image_data.width,
			digits_and_letters_image_data.height
		},
		vk::image_tiling::linear,
		vk::image_usages{ vk::image_usage::sampled },
		array{ queue_family_index },
		vk::initial_layout{ vk::image_layout::preinitialized }
	);
	on_scope_exit destroy_digits_and_letters_image = [&] {
		vk::destroy_image(instance, device, digits_and_letters_image);
	};

	print::out("\"digits_and_letters.png\" image created\n");

	vk::memory_requirements digits_and_letters_memory_requirements
		= vk::get_image_memory_requirements(
			instance, device, digits_and_letters_image
		);

	vk::memory_type_index digits_and_letters_memory_type_index
		= vk::find_first_memory_type_index(
			instance, physical_device,
			vk::memory_properties {
				vk::memory_property::host_visible,
				vk::memory_property::device_local
			},
			digits_and_letters_memory_requirements.memory_type_indices
		);

	handle<vk::device_memory> digits_and_letters_memory = vk::allocate_memory(
		instance, device,
		vk::memory_size{ digits_and_letters_memory_requirements.size },
		digits_and_letters_memory_type_index
	);
	on_scope_exit destroy_digits_and_letters_memory = [&] {
		vk::free_memory(instance, device, digits_and_letters_memory);
	};

	print::out("memory for \"digits_and_letters.png\" is allocated\n");
	
	{
		uint8* mem_ptr;
		vk::map_memory(
			instance, device,
			digits_and_letters_memory,
			digits_and_letters_memory_requirements.size,
			(void**) &mem_ptr
		);

		digits_and_letters_image_data.bytes.as_span().copy_to(span {
			mem_ptr, (uint64) digits_and_letters_memory_requirements.size
		});

		vk::flush_mapped_memory_range(
			instance, device,
			digits_and_letters_memory,
			digits_and_letters_memory_requirements.size
		);

		vk::unmap_memory(instance, device, digits_and_letters_memory);
	}

	print::out("data for \"digits_and_letters.png\" is flushed\n");

	vk::bind_image_memory(
		instance, device,
		digits_and_letters_image, digits_and_letters_memory
	);
	print::out("memory for \"digits_and_letters.png\" is bound\n");

	handle<vk::image_view> digits_and_letters_image_view
		= vk::create_image_view(
			instance, device, digits_and_letters_image,
			vk::format::r8_unorm,
			vk::image_view_type::two_d
		);
	on_scope_exit destroy_digits_and_letters_image_view = [&] {
		vk::destroy_image_view(instance, device, digits_and_letters_image_view);
	};

	print::out("image view for \"digits_and_letters.png\" image is created\n");

	handle<vk::sampler> digits_and_letters_sampler = vk::create_sampler(
		instance, device,
		vk::mag_filter{ vk::filter::linear },
		vk::min_filter{ vk::filter::linear },
		vk::mipmap_mode::nearest,
		vk::address_mode_u{ vk::address_mode::clamp_to_edge },
		vk::address_mode_v{ vk::address_mode::clamp_to_edge },
		vk::address_mode_w{ vk::address_mode::clamp_to_edge }
	);
	on_scope_exit destroy_digits_and_letters_sampler = [&] {
		vk::destroy_sampler(instance, device, digits_and_letters_sampler);
	};

	print::out("sampler for \"digits_and_letters.png\" image is created\n");

	handle<vk::descriptor_pool> descriptor_pool
		= vk::create_descriptor_pool(
			instance, device,
			vk::max_sets{ 1 },
			array { vk::descriptor_pool_size {
				vk::descriptor_type::combined_image_sampler,
				vk::descriptor_count{ 1 }
			}}
		);
	on_scope_exit destroy_descriptor_pool = [&] {
		vk::destroy_descriptor_pool(instance, device, descriptor_pool);
	};

	print::out("descriptor pool created\n");

	handle<vk::descriptor_set_layout> descriptor_set_layout
		= vk::create_descriptor_set_layout(
			instance, device,
			array { vk::descriptor_set_layout_binding {
				vk::descriptor_binding{ 0 },
				vk::descriptor_type::combined_image_sampler,
				vk::descriptor_count{ 1 },
				vk::shader_stages{ vk::shader_stage::fragment }
			}}
		);
	on_scope_exit destroy_descriptor_set_layout = [&] {
		vk::destroy_descriptor_set_layout(
			instance, device, descriptor_set_layout
		);
	};

	print::out("descriptor set layout created\n");

	handle<vk::descriptor_set> descriptor_set
		= vk::allocate_descriptor_set(
			instance, device, descriptor_pool, descriptor_set_layout
		);

	print::out("descriptor set allocated\n");

	array attachment_references {
		vk::color_attachment_reference {
			0,
			vk::image_layout::color_attachment_optimal
		}
	};

	handle<vk::render_pass> render_pass = vk::create_render_pass(
		instance, device,
		array { vk::attachment_description {
			surface_format.format,
			vk::load_op{ vk::attachment_load_op::clear },
			vk::store_op{ vk::attachment_store_op::store },
			vk::final_layout{ vk::image_layout::present_src }
		}},
		array {
			vk::subpass_description{ attachment_references }
		},
		array {
			vk::subpass_dependency {
				vk::src_subpass{ vk::subpass_external },
				vk::dst_subpass{ 0 },
				vk::src_stages{ vk::pipeline_stage::color_attachment_output },
				vk::dst_stages{ vk::pipeline_stage::color_attachment_output }
			}
		}
	);
	on_scope_exit destroy_render_pass = [&] {
		vk::destroy_render_pass(instance, device, render_pass);
	};

	print::out("render pass created\n");

	posix::memory_for_range_of<uint8> tile_vert_data
		= read_file(c_string{ "tile.vert.spv" });

	handle<vk::shader_module> tile_vert_shader_module =
		vk::create_shader_module(
			instance, device,
			vk::code{ (uint32*) tile_vert_data.iterator() },
			vk::code_size{ tile_vert_data.size() }
		);
	on_scope_exit destroy_tile_vert_shader_module = [&] {
		vk::destroy_shader_module(instance, device, tile_vert_shader_module);
	};

	print::out("tile.vert shader module created\n");

	posix::memory_for_range_of<uint8> tile_frag_data
		= read_file(c_string{ "tile.frag.spv" });

	handle<vk::shader_module> tile_frag_shader_module =
		vk::create_shader_module(
			instance, device,
			vk::code{ (uint32*) tile_frag_data.iterator() },
			vk::code_size{ tile_frag_data.size() }
		);
	on_scope_exit destroy_tile_frag_shader_module = [&] {
		vk::destroy_shader_module(instance, device, tile_frag_shader_module);
	};

	print::out("tile.frag shader module created\n");

	handle<vk::pipeline_layout> pipeline_layout = vk::create_pipeline_layout(
		instance, device,
		array { descriptor_set_layout },
		array { vk::push_constant_range {
			.stages {
				vk::shader_stage::vertex,
				vk::shader_stage::fragment
			},
			.offset = 0,
			.size = sizeof(push_constant_t)
		}}
	);
	on_scope_exit destroy_pipeline_layout = [&] {
		vk::destroy_pipeline_layout(instance, device, pipeline_layout);
	};

	print::out("pipeline layout created\n");

	vk::pipeline_color_blend_attachment_state pcbas {
		vk::enable_blend{ false }
	};

	auto dynamic_states = array {
		vk::dynamic_state::viewport, vk::dynamic_state::scissor
	};

	handle<vk::pipeline> pipeline = vk::create_graphics_pipelines(
		instance, device,
		pipeline_layout, render_pass, vk::subpass{ 0 },
		vk::pipeline_input_assembly_state_create_info {
			.topology = vk::primitive_topology::triangle_list
		},
		array {
			vk::pipeline_shader_stage_create_info {
				vk::shader_stage::vertex,
				tile_vert_shader_module,
				vk::entrypoint_name{ "main" }
			},
			vk::pipeline_shader_stage_create_info {
				vk::shader_stage::fragment,
				tile_frag_shader_module,
				vk::entrypoint_name{ "main" }
			}
		},
		vk::pipeline_multisample_state_create_info{},
		vk::pipeline_vertex_input_state_create_info{},
		vk::pipeline_rasterization_state_create_info {
			vk::polygon_mode::fill,
			vk::cull_mode::back,
			vk::front_face::counter_clockwise
		},
		vk::pipeline_color_blend_state_create_info {
			vk::logic_op::copy,
			span{ &pcbas }
		},
		vk::pipeline_viewport_state_create_info {
			vk::viewport_count{ 1 }, vk::scissor_count{ 1 }
		},
		vk::pipeline_dynamic_state_create_info { dynamic_states }
	);
	on_scope_exit destroy_pipeline = [&] {
		vk::destroy_pipeline(instance, device, pipeline);
	};

	print::out("pipeline created\n");

	handle<vk::command_pool> command_pool = vk::create_command_pool(
		instance, device,
		queue_family_index,
		vk::command_pool_create_flags {
			vk::command_pool_create_flag::reset_command_buffer
		}
	);
	on_scope_exit destroy_command_pool = [&] {
		vk::destroy_command_pool(instance, device, command_pool);
	};

	print::out("command pool created\n");

	handle<vk::queue> queue = vk::get_device_queue(
		instance, device,
		queue_family_index,
		vk::queue_index{ 0 }
	);

	print::out("queue received\n");

	{
		handle<vk::command_buffer> change_layout_command_buffer
			= vk::allocate_command_buffer(
				instance, device, command_pool,
				vk::command_buffer_level::primary
			);

		vk::begin_command_buffer(
			instance, device, change_layout_command_buffer,
			vk::command_buffer_usages {
				vk::command_buffer_usage::one_time_submit
			}
		);
		vk::cmd_pipeline_barrier(
			instance, device, change_layout_command_buffer,
			vk::src_stages{ vk::pipeline_stage::all_commands },
			vk::dst_stages{ vk::pipeline_stage::all_commands },
			array{ vk::image_memory_barrier {
				.src_access = { vk::access::memory_write },
				.dst_access = { vk::access::shader_read },
				.old_layout = { vk::image_layout::preinitialized },
				.new_layout = { vk::image_layout::shader_read_only_optimal },
				.image = digits_and_letters_image.underlying()
			}}
		);
		vk::end_command_buffer(instance, device, change_layout_command_buffer);
		vk::queue_submit(instance, device, queue, change_layout_command_buffer);
	}

	vk::update_descriptor_set(
		instance, device,
		vk::write_descriptor_set {
			descriptor_set,
			vk::dst_binding{ 0 },
			vk::descriptor_type::combined_image_sampler,
			array{ vk::descriptor_image_info {
				digits_and_letters_image_view,
				digits_and_letters_sampler,
				vk::image_layout::shader_read_only_optimal
			}}
		}
	);

	print::out("descriptor set updated\n");

	handle<vk::swapchain> swapchain{};
	on_scope_exit destroy_swapchain = [&] {
		if(swapchain.is_valid()) {
			vk::destroy_swapchain(instance, device, swapchain);
		}
	};

	print::out.flush();

	while(!window->should_close()) {
		auto window_size = window->get_size();
		vk::extent<2> extent {
			(unsigned) window_size[0],
			(unsigned) window_size[1]
		};
		push_constant.size[0] = window_size[0];
		push_constant.size[1] = window_size[1];

		handle<vk::swapchain> prev_swapchain = swapchain;

		swapchain = vk::create_swapchain(
			instance, device, surface,
			vk::min_image_count{ 2 },
			extent,
			surface_format.format,
			surface_format.color_space,
			vk::image_usages {
				vk::image_usage::color_attachment,
				vk::image_usage::transfer_dst
			},
			vk::sharing_mode::exclusive,
			vk::present_mode::fifo,
			vk::clipped{ true },
			vk::surface_transform::identity,
			vk::composite_alpha::opaque,
			swapchain
		);

		print::out("swapchain created\n");

		if(prev_swapchain.is_valid()) {
			vk::destroy_swapchain(instance, device, prev_swapchain);
		}

		uint32 swapchain_images_count = vk::get_swapchain_image_count(
			instance, device, swapchain
		);
		handle<vk::image> swapchain_images_raw[swapchain_images_count];
		span swapchain_images{ swapchain_images_raw, swapchain_images_count };
		vk::get_swapchain_images(instance, device, swapchain, swapchain_images);

		print::out("swapchain images received\n");

		handle<vk::image_view> image_views_raw[swapchain_images_count];
		span image_views{ image_views_raw, swapchain_images_count };
		for(nuint i = 0; i < swapchain_images_count; ++i) {
			image_views[i] = vk::create_image_view(
				instance, device, swapchain_images[i],
				surface_format.format, vk::image_view_type::two_d
			);
		}
		on_scope_exit destroy_image_views = [&] {
			for(handle<vk::image_view> image_view : image_views) {
				vk::destroy_image_view(instance, device, image_view);
			}
		};

		print::out("image views created\n");

		handle<vk::framebuffer> framebuffers_raw[swapchain_images_count];
		span framebuffers{ framebuffers_raw, swapchain_images_count };
		for(nuint i = 0; i < swapchain_images_count; ++i) {
			framebuffers[i] = vk::create_framebuffer(
				instance, device, render_pass,
				array{ image_views[i] },
				vk::extent<3>{ extent, 1 }
			);
		}
		on_scope_exit destroy_framebuffer = [&] {
			for(handle<vk::framebuffer> framebuffer : framebuffers) {
				vk::destroy_framebuffer(instance, device, framebuffer);
			}
		};

		print::out("framebuffers created\n");

		handle<vk::command_buffer> command_buffers_raw[swapchain_images_count];
		span command_buffers{ command_buffers_raw, swapchain_images_count };
		vk::allocate_command_buffers(
			instance, device, command_pool,
			vk::command_buffer_level::primary,
			command_buffers
		);
		on_scope_exit free_command_buffer = [&] {
			vk::free_command_buffers(
				instance, device, command_pool, command_buffers
			);
		};

		print::out("command buffers allocated\n");

		handle<vk::fence> fences_raw[swapchain_images_count];
		span fences{ fences_raw, swapchain_images_count };
		for(nuint i = 0; i < swapchain_images_count; ++i) {
			fences[i] = vk::create_fence(instance, device);
		}
		on_scope_exit destroy_fences = [&] {
			for(handle<vk::fence> fence : fences) {
				vk::destroy_fence(instance, device, fence);
			}
		};

		print::out.flush();

		handle<vk::semaphore> acquire_semaphore = vk::create_semaphore(
			instance, device
		);
		on_scope_exit destroy_acquire_semaphore = [&] {
			vk::destroy_semaphore(instance, device, acquire_semaphore);
		};
		handle<vk::semaphore> submit_semaphore = vk::create_semaphore(
			instance, device
		);
		on_scope_exit destroy_submit_semaphore = [&] {
			vk::destroy_semaphore(instance, device, submit_semaphore);
		};

		handle<vk::fence> submit_fence = vk::create_fence(instance, device);
		on_scope_exit destroy_submit_fence = [&] {
			vk::destroy_fence(instance, device, submit_fence);
		};

		while(!window->should_close()) {
			glfw_instance.poll_events();

			vk::expected<vk::image_index> acquire_result
				= vk::try_acquire_next_image(
					instance, device, swapchain, acquire_semaphore
				);
			
			auto should_update_swapchain = [&](vk::result result) {
				if(result.success()) return false;
				if(result.suboptimal() || result.out_of_date()) return true;

				posix::abort();
			};

			if(
				acquire_result.is_unexpected() &&
				should_update_swapchain(acquire_result.get_unexpected())
			) {
				break;
			}

			vk::image_index image_index = acquire_result.get_expected();

			vk::wait_for_fence(instance, device, submit_fence);
			vk::reset_fence(instance, device, submit_fence);

			handle<vk::command_buffer> command_buffer
				= command_buffers[image_index];

			vk::begin_command_buffer(
				instance, device, command_buffer,
				vk::command_buffer_usages {
					vk::command_buffer_usage::one_time_submit
				}
			);
			vk::cmd_begin_render_pass(instance, device, command_buffer,
				render_pass,
				framebuffers[image_index],
				vk::render_area{ extent },
				vk::clear_value{ vk::clear_color_value{} }
			);
			vk::cmd_bind_pipeline(instance, device, command_buffer,
				pipeline, vk::pipeline_bind_point::graphics
			);
			vk::cmd_bind_descriptor_sets(instance, device, command_buffer,
				vk::pipeline_bind_point::graphics,
				pipeline_layout,
				array{ descriptor_set }
			);
			vk::cmd_set_scissor(instance, device, command_buffer, extent);
			vk::cmd_set_viewport(instance, device, command_buffer, extent);
			vk::cmd_push_constants(
				instance, device, command_buffer,
				pipeline_layout,
				vk::push_constant_range {
					.stages {
						vk::shader_stage::vertex,
						vk::shader_stage::fragment,
					},
					.size = sizeof(push_constant_t)
				},
				(void*) &push_constant
			);
			vk::cmd_draw(instance, device, command_buffer,
				vk::vertex_count{ 3 * 2 * 3 * 3 }
			);
			vk::cmd_end_render_pass(instance, device, command_buffer);
			vk::end_command_buffer(instance, device, command_buffer);

			vk::queue_submit(
				instance, device, queue, command_buffer,
				vk::wait_semaphore{ acquire_semaphore },
				vk::signal_semaphore{ submit_semaphore },
				vk::signal_fence{ submit_fence }
			);

			vk::result present_result = vk::try_queue_present(
				instance, device, queue,
				swapchain, image_index,
				vk::wait_semaphore{ submit_semaphore }
			);

			if(should_update_swapchain(present_result)) {
				break;
			}
		}

		vk::device_wait_idle(instance, device);
	}
}