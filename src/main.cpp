#include "./handlers.hpp"
#include "./vk_functions.hpp"
#include "./glfw.hpp"
#include "./read_png.hpp"
#include "./read_shader_module.hpp"
#include "./table.hpp"
#include "./state.hpp"
#include "./frame.hpp"

#include <vk.hpp>

#include <glfw/window.hpp>

#include <print/print.hpp>

#include <posix/io.hpp>
#include <posix/memory.hpp>
#include <posix/time.hpp>

#include <on_scope_exit.hpp>
#include <array.hpp>
#include <ranges.hpp>
#include <number.hpp>
#include <numbers.hpp>
#include <list.hpp>


int main() {
	posix::rand_seed(posix::get_ticks());

	table.try_put_random_value();
	table.try_put_random_value();

	if (!glfw_instance.is_vulkan_supported()) {
		print::err("vulkan isn't supported\n");
		return 1;
	}

	init_glfw_window();

	window->set_key_callback(
		+[](
			glfw::window*, glfw::key::code key, int,
			glfw::key::action action, glfw::key::modifiers
		) {
			if (game_state != game_state::waiting_input) {
				return;
			}

			if (action != glfw::key::action::press) return;

			optional<movement_table_t> possible_movement_table{};
			prev_table = table;

			switch (key) {
				case glfw::keys::w :
				case glfw::keys::up :
					possible_movement_table = table.try_move<up>();    break;
				case glfw::keys::s :
				case glfw::keys::down :
					possible_movement_table = table.try_move<down>();  break;
				case glfw::keys::a :
				case glfw::keys::left :
					possible_movement_table = table.try_move<left>();  break;
				case glfw::keys::d :
				case glfw::keys::right :
					possible_movement_table = table.try_move<right>(); break;
			}

			if (possible_movement_table.has_value()) {
				movement_table = possible_movement_table.get();

				table.try_put_random_value();
				game_state = game_state::animating;
				animation_begin_time = posix::get_ticks();
			}
		}
	);

	handle<vk::instance> instance =
		ranges {
			glfw_instance.get_required_instance_extensions(),
			array {
				vk::extension_name { u8"VK_EXT_debug_report" },
				vk::extension_name { u8"VK_EXT_debug_utils" }
			}
		}.concat_view().view_copied_elements_on_stack(
			[](span<vk::extension_name> extension_names) {
				return vk::create_instance(
					vk::application_info {
						vk::api_version { vk::major { 1 }, vk::minor { 0 } }
					},
					extension_names,
					array{ vk::layer_name { u8"VK_LAYER_KHRONOS_validation" } }
				);
			}
		);

	on_scope_exit destroy_instance = [&] {
		vk::destroy_instance(instance);
		print::out("instance is destroyed\n");
	};

	print::out("instance is created\n");

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
				[[maybe_unused]] c_string<utf8::unit> l_prefix,
				c_string<utf8::unit> message,
				[[maybe_unused]] void* user_data
			) -> uint32 {
				print::out("[vk] ", c_string { message }.sized(), "\n");
				return 0;
			}
		);
	on_scope_exit destroy_debug_report_callback = [&] {
		vk::destroy_debug_report_callback(instance, debug_report_callback);
	};

	handle<vk::physical_device> physical_device
		= instance->get_first_physical_device();

	vk::physical_device_properties physical_device_props
		= vk::get_physical_device_properties(instance, physical_device);

	print::out("selected physical device: ", physical_device_props.name, "\n");

	handle<vk::surface> surface = window->create_surface(instance);
	on_scope_exit destroy_surface = [&] {
		vk::destroy_surface(instance, surface);
		print::out("surface is destroyed\n");
	};

	print::out("surface is created\n");

	vk::surface_format surface_format =
		vk::try_choose_physical_device_surface_format(
			instance, physical_device, surface,
			array {
				vk::surface_format {
					vk::format::r8_g8_b8_a8_srgb,
					vk::color_space::srgb_nonlinear
				}
			}
		).if_has_no_value([]() {
			print::err("couldn't choose surface format");
			posix::abort();
		}).get();

	print::out("surface format is selected\n");

	vk::queue_family_index queue_family_index =
		vk::view_physical_device_queue_family_properties(
			instance, physical_device,
			[&](span<vk::queue_family_properties> props_span) {
				for (auto [index, props] : props_span.indexed_view()) {
					bool graphics = props.flags & vk::queue_flag::graphics;
					bool supports_surface =
						vk::get_physical_device_surface_support(
							instance, physical_device,
							surface,
							vk::queue_family_index { (uint32) index }
						);

					if (graphics && supports_surface) {
						return vk::queue_family_index { (uint32) index };
					}
				};
				return vk::queue_family_ignored;
			}
		);

	print::out("queue family index is selected\n");

	array queue_priorities { vk::queue_priority { 1.0F } };

	handle<vk::device> device = vk::create_device(
		instance, physical_device,
		array { vk::queue_create_info {
			queue_family_index,
			queue_priorities
		}},
		array { vk::extension_name { u8"VK_KHR_swapchain" } }
	);
	vk::debug_utils::set_object_name(
		instance, device,
		device,
		vk::debug_utils::object_name { u8"device" }
	);
	on_scope_exit destroy_device = [&] {
		vk::destroy_device(instance, device);
		print::out("device is destroyed\n");
	};

	print::out("device is created\n");

	png_data digits_and_letters_image_data =
		read_png(c_string { "digits_and_letters.png" });

	print::out("\"digits_and_letters.png\" read\n");

	handle<vk::image> digits_and_letters_image = vk::create_image(
		instance, device,
		vk::format::r8_unorm,
		vk::extent<2> {
			digits_and_letters_image_data.width,
			digits_and_letters_image_data.height
		},
		vk::image_tiling::linear,
		vk::image_usages { vk::image_usage::sampled },
		vk::initial_layout { vk::image_layout::preinitialized }
	);
	on_scope_exit destroy_digits_and_letters_image = [&] {
		vk::destroy_image(instance, device, digits_and_letters_image);
		print::out("\"digits_and_letters.png\" image is destroyed\n");
	};

	print::out("\"digits_and_letters.png\" image is created\n");

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
		vk::memory_size { digits_and_letters_memory_requirements.size },
		digits_and_letters_memory_type_index
	);
	on_scope_exit destroy_digits_and_letters_memory = [&] {
		vk::free_memory(instance, device, digits_and_letters_memory);
		print::out("memory for \"digits_and_letters.png\" is freed\n");
	};

	print::out("memory for \"digits_and_letters.png\" is allocated\n");
	
	{
		uint8* mem_ptr = vk::map_memory(
			instance, device,
			digits_and_letters_memory,
			digits_and_letters_memory_requirements.size
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
		print::out(
			"image view for \"digits_and_letters.png\" image is destroyed\n"
		);
	};

	print::out("image view for \"digits_and_letters.png\" image is created\n");

	handle<vk::sampler> digits_and_letters_sampler = vk::create_sampler(
		instance, device,
		vk::mag_filter { vk::filter::linear },
		vk::min_filter { vk::filter::linear },
		vk::mipmap_mode::nearest,
		vk::address_mode_u { vk::address_mode::clamp_to_edge },
		vk::address_mode_v { vk::address_mode::clamp_to_edge },
		vk::address_mode_w { vk::address_mode::clamp_to_edge }
	);
	on_scope_exit destroy_digits_and_letters_sampler = [&] {
		vk::destroy_sampler(instance, device, digits_and_letters_sampler);
		print::out(
			"sampler for \"digits_and_letters.png\" image is destroyed\n"
		);
	};

	print::out("sampler for \"digits_and_letters.png\" image is created\n");

	handle<vk::descriptor_pool> descriptor_pool
		= vk::create_descriptor_pool(
			instance, device,
			vk::max_sets { 2 },
			array {
				vk::descriptor_pool_size {
					vk::descriptor_type::uniform_buffer,
					vk::descriptor_count { 2 }
				},
				vk::descriptor_pool_size {
					vk::descriptor_type::combined_image_sampler,
					vk::descriptor_count { 1 }
				}
			}
		);
	on_scope_exit destroy_descriptor_pool = [&] {
		vk::destroy_descriptor_pool(instance, device, descriptor_pool);
		print::out("descriptor pool is destroyed\n");
	};

	print::out("descriptor pool is created\n");

	handle<vk::descriptor_set_layout> tile_descriptor_set_layout
		= vk::create_descriptor_set_layout(
			instance, device,
			array { vk::descriptor_set_layout_binding {
				vk::descriptor_binding { 0 },
				vk::descriptor_type::uniform_buffer,
				vk::descriptor_count { 1 },
				vk::shader_stages {
					vk::shader_stage::vertex
				}
			}}
		);

	on_scope_exit destroy_tile_descriptor_set_layout = [&] {
		vk::destroy_descriptor_set_layout(
			instance, device, tile_descriptor_set_layout
		);
		print::out("\"tile\" descriptor set layout is destroyed\n");
	};
	print::out("\"tile\" descriptor set layout is created\n");

	handle<vk::descriptor_set_layout> digits_and_letters_descriptor_set_layout
		= vk::create_descriptor_set_layout(
			instance, device,
			array {
				vk::descriptor_set_layout_binding {
					vk::descriptor_binding { 0 },
					vk::descriptor_type::uniform_buffer,
					vk::descriptor_count { 1 },
					vk::shader_stages {
						vk::shader_stage::vertex
					}
				},
				vk::descriptor_set_layout_binding {
					vk::descriptor_binding { 1 },
					vk::descriptor_type::combined_image_sampler,
					vk::descriptor_count { 1 },
					vk::shader_stages {
						vk::shader_stage::fragment
					}
				}
			}
		);

	on_scope_exit destroy_digits_and_letters_descriptor_set_layout = [&] {
		vk::destroy_descriptor_set_layout(
			instance, device, digits_and_letters_descriptor_set_layout
		);
		print::out(
			"\"digits_and_letters\" descriptor set layout is destroyed\n"
		);
	};
	print::out("\"digits_and_letters\" descriptor set layout is created\n");

	handle<vk::descriptor_set> tile_descriptor_set
		= vk::allocate_descriptor_set(
			instance, device, descriptor_pool, tile_descriptor_set_layout
		);

	handle<vk::descriptor_set> digits_and_letters_descriptor_set
		= vk::allocate_descriptor_set(
			instance, device, descriptor_pool,
			digits_and_letters_descriptor_set_layout
		);

	print::out("descriptor sets are allocated\n");

	array depth_attachment_references {
		vk::depth_stencil_attachment_reference {
			0,
			vk::image_layout::depth_stencil_attachment_optimal
		}
	};

	array color_attachment_references {
		vk::color_attachment_reference {
			1,
			vk::image_layout::color_attachment_optimal
		}
	};

	handle<vk::render_pass> tile_render_pass = vk::create_render_pass(
		instance, device,
		array {
			vk::attachment_description {
				vk::format::d32_sfloat,
				vk::initial_layout { vk::image_layout::undefined },
				vk::load_op { vk::attachment_load_op::clear },
				vk::final_layout {
					vk::image_layout::depth_stencil_attachment_optimal
				},
				vk::store_op { vk::attachment_store_op::store },
			},
			vk::attachment_description {
				surface_format.format,
				vk::initial_layout { vk::image_layout::undefined },
				vk::load_op { vk::attachment_load_op::clear },
				vk::final_layout { vk::image_layout::color_attachment_optimal },
				vk::store_op { vk::attachment_store_op::store },
			}
		},
		array {
			vk::subpass_description {
				depth_attachment_references,
				color_attachment_references
			}
		}
	);
	vk::debug_utils::set_object_name(
		instance, device,
		tile_render_pass,
		vk::debug_utils::object_name { u8"\"tile\" render pass" }
	);
	on_scope_exit destroy_tile_render_pass = [&] {
		vk::destroy_render_pass(instance, device, tile_render_pass);
		print::out("\"tile\" render pass is destroyed\n");
	};
	print::out("\"tile\" render pass is created\n");

	handle<vk::render_pass> digits_and_letters_render_pass
		= vk::create_render_pass(
			instance, device,
			array {
				vk::attachment_description {
					vk::format::d32_sfloat,
					vk::initial_layout {
						vk::image_layout::depth_stencil_attachment_optimal
					},
					vk::load_op { vk::attachment_load_op::load },
					vk::final_layout {
						vk::image_layout::depth_stencil_attachment_optimal
					},
					vk::store_op { vk::attachment_store_op::store },
				},
				vk::attachment_description {
					surface_format.format,
					vk::initial_layout {
						vk::image_layout::color_attachment_optimal
					},
					vk::load_op { vk::attachment_load_op::load },
					vk::final_layout { vk::image_layout::present_src },
					vk::store_op { vk::attachment_store_op::store }
				}
			},
			array {
				vk::subpass_description {
					depth_attachment_references,
					color_attachment_references
				}
			}
		);
	vk::debug_utils::set_object_name(
		instance, device,
		digits_and_letters_render_pass,
		vk::debug_utils::object_name { u8"\"digits and letters\" render pass" }
	);
	on_scope_exit destroy_digits_and_letters_render_pass = [&] {
		vk::destroy_render_pass(
			instance, device, digits_and_letters_render_pass
		);
		print::out("\"digits and letters\" render pass is destroyed\n");
	};
	print::out("\"digits and letters\" render pass is created\n");

	handle<vk::shader_module> tile_vert_shader_module
		= read_shader_module(instance, device, c_string{ "tile.vert.spv" });
	on_scope_exit destroy_tile_vert_shader_module = [&] {
		vk::destroy_shader_module(instance, device, tile_vert_shader_module);
		print::out("\"tile.vert\" shader module is destroyed\n");
	};

	print::out("\"tile.vert\" shader module is created\n");

	handle<vk::shader_module> tile_frag_shader_module
		= read_shader_module(instance, device, c_string{ "tile.frag.spv" });
	on_scope_exit destroy_tile_frag_shader_module = [&] {
		vk::destroy_shader_module(instance, device, tile_frag_shader_module);
		print::out("\"tile.frag\" shader module is destroyed\n");
	};
	print::out("\"tile.frag\" shader module is created\n");

	handle<vk::shader_module> digits_and_letters_vert_shader_module
		= read_shader_module(
			instance, device, c_string{ "digits_and_letters.vert.spv" }
		);
	on_scope_exit destroy_digits_and_letters_vert_shader_module = [&] {
		vk::destroy_shader_module(
			instance, device, digits_and_letters_vert_shader_module
		);
		print::out("\"digits_and_letters.vert\" shader module is destroyed\n");
	};

	print::out("\"digits_and_letters.vert\" shader module is created\n");

	handle<vk::shader_module> digits_and_letters_frag_shader_module
		= read_shader_module(
			instance, device, c_string{ "digits_and_letters.frag.spv" }
		);
	on_scope_exit destroy_digits_and_letters_frag_shader_module = [&] {
		vk::destroy_shader_module(
			instance, device, digits_and_letters_frag_shader_module
		);
		print::out("\"digits_and_letters.frag\" shader module is destroyed\n");
	};

	print::out("\"digits_and_letters.frag\" shader module is created\n");

	handle<vk::pipeline_layout> tile_pipeline_layout
		= vk::create_pipeline_layout(
			instance, device,
			array { tile_descriptor_set_layout },
			array {
				vk::push_constant_range {
					vk::shader_stages {
						vk::shader_stage::vertex
					},
					vk::size { 2 * sizeof(uint32) }
				}
			}
		);
	on_scope_exit destroy_tile_pipeline_layout = [&] {
		vk::destroy_pipeline_layout(instance, device, tile_pipeline_layout);
		print::out("\"tile\" pipeline layout is destroyed\n");
	};
	print::out("\"tile\" pipeline layout is created\n");

	handle<vk::pipeline_layout> digits_and_letters_pipeline_layout
		= vk::create_pipeline_layout(
			instance, device,
			array { digits_and_letters_descriptor_set_layout },
			array {
				vk::push_constant_range {
					vk::shader_stages {
						vk::shader_stage::vertex
					},
					vk::size { 2 * sizeof(uint32) }
				}
			}
		);
	on_scope_exit destroy_digits_and_letters_pipeline_layout = [&] {
		vk::destroy_pipeline_layout(
			instance, device, digits_and_letters_pipeline_layout
		);
		print::out("\"digits_and_letters\" pipeline layout is destroyed\n");
	};
	print::out("\"digits_and_letters\" pipeline layout is created\n");

	array dynamic_states {
		vk::dynamic_state::viewport, vk::dynamic_state::scissor
	};

	handle<vk::pipeline> tile_pipeline = vk::create_graphics_pipelines(
		instance, device,
		tile_pipeline_layout, tile_render_pass, vk::subpass { 0 },
		vk::pipeline_input_assembly_state_create_info {
			.topology = vk::primitive_topology::triangle_list
		},
		array {
			vk::pipeline_shader_stage_create_info {
				vk::shader_stage::vertex,
				tile_vert_shader_module,
				vk::entrypoint_name { u8"main" }
			},
			vk::pipeline_shader_stage_create_info {
				vk::shader_stage::fragment,
				tile_frag_shader_module,
				vk::entrypoint_name { u8"main" }
			}
		},
		vk::pipeline_depth_stencil_state_create_info {
			.enable_depth_test = true,
			.enable_depth_write = true,
			.depth_compare_op = vk::compare_op::less_or_equal
		},
		vk::pipeline_multisample_state_create_info {},
		vk::pipeline_vertex_input_state_create_info {},
		vk::pipeline_rasterization_state_create_info {
			vk::polygon_mode::fill,
			vk::cull_mode::back,
			vk::front_face::counter_clockwise
		},
		vk::pipeline_color_blend_state_create_info {
			vk::logic_op::copy,
			array { vk::pipeline_color_blend_attachment_state {
				vk::enable_blend { false }
				/*vk::enable_blend { true },
				vk::src_color_blend_factor { vk::blend_factor::src_alpha },
				vk::dst_color_blend_factor { vk::blend_factor::one_minus_src_alpha },
				vk::color_blend_op { vk::blend_op::add },
				vk::src_alpha_blend_factor { vk::blend_factor::one },
				vk::dst_alpha_blend_factor { vk::blend_factor::one_minus_src_alpha },
				vk::alpha_blend_op { vk::blend_op::add }*/
			}}
		},
		vk::pipeline_viewport_state_create_info {
			vk::viewport_count { 1 }, vk::scissor_count { 1 }
		},
		vk::pipeline_dynamic_state_create_info { dynamic_states }
	);
	on_scope_exit destroy_tile_pipeline = [&] {
		vk::destroy_pipeline(instance, device, tile_pipeline);
		print::out("\"tile\" pipeline is destroyed\n");
	};
	print::out("\"tile\" pipeline is created\n");

	handle<vk::pipeline> digits_and_letters_pipeline
	= vk::create_graphics_pipelines(
		instance, device,
		digits_and_letters_pipeline_layout, digits_and_letters_render_pass,
		vk::subpass{ 0 },
		vk::pipeline_input_assembly_state_create_info {
			.topology = vk::primitive_topology::triangle_list
		},
		array {
			vk::pipeline_shader_stage_create_info {
				vk::shader_stage::vertex,
				digits_and_letters_vert_shader_module,
				vk::entrypoint_name { u8"main" }
			},
			vk::pipeline_shader_stage_create_info {
				vk::shader_stage::fragment,
				digits_and_letters_frag_shader_module,
				vk::entrypoint_name { u8"main" }
			}
		},
		vk::pipeline_depth_stencil_state_create_info {
			.enable_depth_test = true,
			.enable_depth_write = true,
			.depth_compare_op = vk::compare_op::less_or_equal
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
			array { vk::pipeline_color_blend_attachment_state {
				vk::enable_blend { true },
				vk::src_color_blend_factor { vk::blend_factor::src_alpha },
				vk::dst_color_blend_factor { vk::blend_factor::one_minus_src_alpha },
				vk::color_blend_op { vk::blend_op::add },
				vk::src_alpha_blend_factor { vk::blend_factor::one },
				vk::dst_alpha_blend_factor { vk::blend_factor::zero },
				vk::alpha_blend_op { vk::blend_op::add }
			}},
		},
		vk::pipeline_viewport_state_create_info {
			vk::viewport_count { 1 }, vk::scissor_count { 1 }
		},
		vk::pipeline_dynamic_state_create_info { dynamic_states }
	);
	on_scope_exit destroy_digits_and_letters_pipeline = [&] {
		vk::destroy_pipeline(instance, device, digits_and_letters_pipeline);
		print::out("\"digits_and_letters\" pipeline is destroyed\n");
	};
	print::out("\"digits_and_letters\" pipeline is created\n");

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

	print::out("command pool is created\n");

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
			vk::src_stages { vk::pipeline_stage::all_commands },
			vk::dst_stages { vk::pipeline_stage::all_commands },
			array{ vk::image_memory_barrier {
				.src_access = { vk::access::host_write },
				.dst_access = { vk::access::shader_read },
				.old_layout = { vk::image_layout::preinitialized },
				.new_layout = { vk::image_layout::shader_read_only_optimal },
				.image = digits_and_letters_image.underlying()
			}}
		);
		vk::end_command_buffer(instance, device, change_layout_command_buffer);
		vk::queue_submit(instance, device, queue, change_layout_command_buffer);
	}

	handle<vk::buffer> tile_uniform_buffer = vk::create_buffer(
		instance, device, vk::buffer_size { 65536 },
		vk::buffer_usages {
			vk::buffer_usage::transfer_src,
			vk::buffer_usage::transfer_dst,
			vk::buffer_usage::uniform_buffer
		}
	);
	vk::debug_utils::set_object_name(
		instance, device,
		tile_uniform_buffer,
		vk::debug_utils::object_name { u8"\"tile\" uniform buffer" }
	);
	on_scope_exit destroy_tile_uniform_buffer = [&] {
		vk::destroy_buffer(instance, device, tile_uniform_buffer);
	};
	vk::memory_requirements tile_uniform_buffer_memory_requirements =
		vk::get_memory_requirements(instance, device, tile_uniform_buffer);

	handle<vk::device_memory> tile_uniform_buffer_memory = vk::allocate_memory(
		instance, device,
		tile_uniform_buffer_memory_requirements.size,
		vk::find_first_memory_type_index(
			instance, physical_device, vk::memory_properties {
				vk::memory_property::device_local
			},
			tile_uniform_buffer_memory_requirements.memory_type_indices
		)
	);
	on_scope_exit free_tile_uniform_buffer_memory = [&] {
		vk::free_memory(instance, device, tile_uniform_buffer_memory);
	};

	handle<vk::buffer> digits_and_letters_uniform_buffer = vk::create_buffer(
		instance, device, vk::buffer_size { 65536 },
		vk::buffer_usages {
			vk::buffer_usage::transfer_src,
			vk::buffer_usage::transfer_dst,
			vk::buffer_usage::uniform_buffer
		}
	);
	vk::debug_utils::set_object_name(
		instance, device,
		digits_and_letters_uniform_buffer,
		vk::debug_utils::object_name {
			u8"\"digits and letters\" uniform buffer"
		}
	);
	on_scope_exit destroy_digits_and_letters_uniform_buffer = [&] {
		vk::destroy_buffer(instance, device, digits_and_letters_uniform_buffer);
	};

	vk::memory_requirements digits_and_letters_uniform_buffer_mem_requirements
		= vk::get_memory_requirements(
			instance, device, digits_and_letters_uniform_buffer
		);

	handle<vk::device_memory> digits_and_letters_uniform_buffer_memory
		= vk::allocate_memory(
			instance, device,
			digits_and_letters_uniform_buffer_mem_requirements.size,
			vk::find_first_memory_type_index(
				instance, physical_device, vk::memory_properties {
					vk::memory_property::device_local
				},
				digits_and_letters_uniform_buffer_mem_requirements
					.memory_type_indices
			)
		);
	on_scope_exit free_digits_and_letters_uniform_buffer_memory = [&] {
		vk::free_memory(
			instance, device, digits_and_letters_uniform_buffer_memory
		);
	};

	vk::bind_buffer_memory(
		instance, device,
		tile_uniform_buffer,
		tile_uniform_buffer_memory
	);

	vk::bind_buffer_memory(
		instance, device,
		digits_and_letters_uniform_buffer,
		digits_and_letters_uniform_buffer_memory
	);

	vk::update_descriptor_set(
		instance, device,
		vk::write_descriptor_set {
			tile_descriptor_set,
			vk::dst_binding { 0 },
			vk::descriptor_type::uniform_buffer,
			array { vk::descriptor_buffer_info {
				tile_uniform_buffer,
				vk::memory_size { 65536 }
			}}
		}
	);

	vk::update_descriptor_sets(
		instance, device,
		array {
			vk::write_descriptor_set {
				digits_and_letters_descriptor_set,
				vk::dst_binding { 0 },
				vk::descriptor_type::uniform_buffer,
				array { vk::descriptor_buffer_info {
					digits_and_letters_uniform_buffer,
					vk::memory_size { 65536 }
				}}
			},
			vk::write_descriptor_set {
				digits_and_letters_descriptor_set,
				vk::dst_binding { 1 },
				vk::descriptor_type::combined_image_sampler,
				array { vk::descriptor_image_info {
					digits_and_letters_image_view,
					digits_and_letters_sampler,
					vk::image_layout::shader_read_only_optimal
				}}
			}
		}
	);

	print::out("descriptor sets are updated\n");

	frame(
		instance, physical_device, device, surface, surface_format,
		command_pool, queue,
		
		tile_render_pass, tile_pipeline,
		tile_pipeline_layout, tile_uniform_buffer, tile_descriptor_set,
		
		digits_and_letters_render_pass, digits_and_letters_pipeline,
		digits_and_letters_pipeline_layout, digits_and_letters_uniform_buffer, digits_and_letters_descriptor_set
	);
}