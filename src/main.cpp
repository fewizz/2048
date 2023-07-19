#include "./handlers.hpp"
#include "./vk_functions.hpp"
#include "./glfw.hpp"
#include "./read_file.hpp"
#include "./read_png.hpp"
#include "./read_shader_module.hpp"
#include "./table.hpp"

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

static enum class game_state_t {
	waiting_input, animating
} game_state = game_state_t::waiting_input;

posix::seconds_and_nanoseconds animation_begin_time{};
static constexpr nuint animation_ms = 100;
table_t prev_table{};
movement_table_t movement_table;

int main() {
	{
		auto [seconds, nanoseconds]
			= posix::monolitic_clock.secods_and_nanoseconds();

		posix::rand_seed(seconds + nanoseconds);
	}

	table.try_put_random_value();
	table.try_put_random_value();

	if(!glfw_instance.is_vulkan_supported()) {
		print::err("vulkan isn't supported\n");
		return 1;
	}

	init_glfw_window();

	window->set_key_callback(
		+[](
			glfw::window*, glfw::key::code key, int,
			glfw::key::action action, glfw::key::modifiers
		) {
			if(game_state != game_state_t::waiting_input) {
				return;
			}

			if(action != glfw::key::action::press) return;

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

			if(possible_movement_table.has_value()) {
				movement_table = possible_movement_table.get();

				table.try_put_random_value();
				game_state = game_state_t::animating;
				animation_begin_time
					= posix::monolitic_clock.secods_and_nanoseconds();
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
			[](span<vk::extension_name> extensions_names) {
				return vk::create_instance(
					vk::application_info {
						vk::api_version { vk::major { 1 }, vk::minor { 0 } }
					},
					extensions_names,
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
				[[maybe_unused]] c_string_of_unknown_size<utf8::unit> l_prefix,
				c_string_of_unknown_size<utf8::unit> message,
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

	vk::queue_family_index queue_family_index = vk::queue_family_ignored;

	vk::view_physical_device_queue_family_properties(
		instance, physical_device,
		[&](span<vk::queue_family_properties> props_span) {
			props_span.for_each_indexed([&](auto props, uint32 index) {
				bool graphics = props.flags & vk::queue_flag::graphics;
				bool supports_surface =
					vk::get_physical_device_surface_support(
						instance, physical_device,
						surface,
						vk::queue_family_index { index }
					);

				if(graphics && supports_surface) {
					queue_family_index = index;
					return loop_action::stop;
				}

				return loop_action::next;
			});
		}
	);

	print::out("queue family index is selected\n");

	array queue_priorities { vk::queue_priority { 1.0F } };

	handle<vk::device> device = vk::create_device(
		instance, physical_device,
		array { vk::queue_create_info {
			queue_family_index,
			vk::queue_count { 1 },
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
		vk::image_type::two_d,
		vk::format::r8_unorm,
		vk::extent<3> {
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

	vk::pipeline_color_blend_attachment_state tile_pcbas {
		vk::enable_blend { false }
		/*vk::enable_blend { true },
		vk::src_color_blend_factor { vk::blend_factor::src_alpha },
		vk::dst_color_blend_factor { vk::blend_factor::one_minus_src_alpha },
		vk::color_blend_op { vk::blend_op::add },
		vk::src_alpha_blend_factor { vk::blend_factor::one },
		vk::dst_alpha_blend_factor { vk::blend_factor::one_minus_src_alpha },
		vk::alpha_blend_op { vk::blend_op::add }*/
	};

	auto dynamic_states = array {
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
			span{ &tile_pcbas }
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

	vk::pipeline_color_blend_attachment_state digits_and_letters_pcbas {
		vk::enable_blend { true },
		vk::src_color_blend_factor { vk::blend_factor::src_alpha },
		vk::dst_color_blend_factor { vk::blend_factor::one_minus_src_alpha },
		vk::color_blend_op { vk::blend_op::add },
		vk::src_alpha_blend_factor { vk::blend_factor::one },
		vk::dst_alpha_blend_factor { vk::blend_factor::zero },
		vk::alpha_blend_op { vk::blend_op::add }
	};

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
			span{ &digits_and_letters_pcbas }
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

	handle<vk::device_memory> tile_uniform_buffer_memory = vk::allocate_memory(
		instance, device, vk::memory_size { 65536 },
		vk::find_first_memory_type_index(
			instance, physical_device, vk::memory_properties {
				vk::memory_property::device_local
			}
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
	on_scope_exit destroy_digits_and_letters__uniform_buffer = [&] {
		vk::destroy_buffer(instance, device, digits_and_letters_uniform_buffer);
	};

	handle<vk::device_memory> digits_and_letters_uniform_buffer_memory
		= vk::allocate_memory(
			instance, device, vk::memory_size { 65536 },
			vk::find_first_memory_type_index(
				instance, physical_device, vk::memory_properties {
					vk::memory_property::device_local
				}
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

	handle<vk::swapchain> swapchain{};
	on_scope_exit destroy_swapchain = [&] {
		if(swapchain.is_valid()) {
			vk::destroy_swapchain(instance, device, swapchain);
		}
		print::out("swapchain is destroyed\n");
	};

	print::out.flush();

	while(!window->should_close()) {
		handle<vk::swapchain> prev_swapchain = swapchain;

		vk::surface_capabilities surface_caps =
			vk::get_physical_device_surface_capabilities(
				instance, physical_device, surface
			);

		vk::extent<2> extent = surface_caps.current_extent;

		if(extent == 0u) { // on windows, window is minimised. TODO
			glfw_instance.poll_events();
			continue;
		}
		if(extent == -1u) { // on linux, TODO
			extent = extent.clamp(
				surface_caps.min_image_extent,
				surface_caps.max_image_extent
			);
		}

		swapchain = vk::create_swapchain(
			instance, device, surface,
			vk::min_image_count {
				number { 2u }.clamp(
					surface_caps.min_image_count,
					surface_caps.max_image_count
				)
			},
			extent,
			surface_format.format,
			surface_format.color_space,
			vk::image_usages {
				vk::image_usage::color_attachment,
				vk::image_usage::transfer_dst
			},
			vk::sharing_mode::exclusive,
			vk::present_mode::fifo,
			vk::clipped { true },
			vk::surface_transform::identity,
			vk::composite_alpha::opaque,
			swapchain
		);

		print::out("swapchain is (re)created\n");

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
			print::out("swapchain's image views are freed\n");
		};
		print::out("swapchain's image views are created\n");

		handle<vk::image> depth_images_raw[swapchain_images_count];
		span depth_images { depth_images_raw, swapchain_images_count };

		for(nuint i = 0; i < swapchain_images_count; ++i) {
			depth_images[i] = vk::create_image(instance, device,
				vk::image_type::two_d,
				vk::format::d32_sfloat,
				vk::extent<3> { extent, 1 },
				vk::image_tiling::optimal,
				vk::image_usages { vk::image_usage::depth_stencil_attachment }
			);
		}
		on_scope_exit destroy_depth_images { [&] {
			for(handle<vk::image> depth_image : depth_images) {
				vk::destroy_image(instance, device, depth_image);
			}
			print::out("depth images are destroyed\n");
		}};
		print::out("depth images are created\n");

		vk::memory_requirements depth_image_memory_requirements
			= vk::get_image_memory_requirements(
				instance, device, depth_images[0]
			);

		handle<vk::device_memory> depth_images_memory = vk::allocate_memory(
			instance, device,
			vk::memory_size {
				number {
					(uint64) depth_image_memory_requirements.size
				}.align(
					depth_image_memory_requirements.alignment
				) * swapchain_images_count
			},
			vk::find_first_memory_type_index(
				instance, physical_device,
				vk::memory_properties {
					vk::memory_property::host_visible,
					vk::memory_property::device_local
				},
				depth_image_memory_requirements.memory_type_indices
			)
		);
		on_scope_exit destroy_depth_images_memory { [&] {
			vk::free_memory(instance, device, depth_images_memory);
			print::out("memory for depth images is freed\n");
		}};
		print::out("memory for depth images is allocated\n");

		for(nuint i = 0; i < swapchain_images_count; ++i) {
			vk::bind_image_memory(
				instance, device, depth_images[i],
				depth_images_memory,
				vk::memory_offset {
					i * number {
						(uint64) depth_image_memory_requirements.size
					}.align(
						depth_image_memory_requirements.alignment
					)
				}
			);
		}

		handle<vk::image_view> depth_image_views_raw[swapchain_images_count];
		span depth_image_views {
			depth_image_views_raw, swapchain_images_count
		};

		for(nuint i = 0; i < swapchain_images_count; ++i) {
			depth_image_views[i] = vk::create_image_view(instance, device,
				depth_images[i], vk::format::d32_sfloat,
				vk::image_view_type::two_d,
				vk::image_subresource_range {
					vk::image_aspects { vk::image_aspect::depth }
				}
			);
		}
		on_scope_exit destroy_depth_image_views { [&] {
			for(handle<vk::image_view> depth_image_view : depth_image_views) {
				vk::destroy_image_view(instance, device, depth_image_view);
			}
			print::out("depth image views are destroyed\n");
		}};
		print::out("depth image views are created\n");

		handle<vk::framebuffer> framebuffers_raw[swapchain_images_count];
		span framebuffers { framebuffers_raw, swapchain_images_count };
		for(nuint i = 0; i < swapchain_images_count; ++i) {
			framebuffers[i] = vk::create_framebuffer(
				instance, device, tile_render_pass,
				array {
					depth_image_views[i],
					image_views[i]
				},
				vk::extent<3> { extent, 1 }
			);
		}
		on_scope_exit destroy_framebuffer = [&] {
			for(handle<vk::framebuffer> framebuffer : framebuffers) {
				vk::destroy_framebuffer(instance, device, framebuffer);
			}
			print::out("framebuffers are freed\n");
		};

		print::out("framebuffers are created\n");

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
			print::out("command buffers are freed\n");
		};

		print::out("command buffers are allocated\n");

		handle<vk::fence> fences_raw[swapchain_images_count];
		span fences{ fences_raw, swapchain_images_count };
		for(nuint i = 0; i < swapchain_images_count; ++i) {
			fences[i] = vk::create_fence(instance, device);
		}
		on_scope_exit destroy_fences = [&] {
			for(handle<vk::fence> fence : fences) {
				vk::destroy_fence(instance, device, fence);
			}
			print::out("fences are destroyed\n");
		};

		print::out("fences are created\n");

		handle<vk::semaphore> acquire_semaphore = vk::create_semaphore(
			instance, device
		);
		on_scope_exit destroy_acquire_semaphore = [&] {
			vk::destroy_semaphore(instance, device, acquire_semaphore);
			print::out("acquire semaphore is destroyed\n");
		};

		print::out("acquire semaphore is created\n");

		handle<vk::semaphore> submit_semaphore = vk::create_semaphore(
			instance, device
		);
		on_scope_exit destroy_submit_semaphore = [&] {
			vk::destroy_semaphore(instance, device, submit_semaphore);
			print::out("submit semaphore is destroyed\n");
		};

		print::out("submit semaphore is created\n");

		handle<vk::fence> submit_fence = vk::create_fence(
			instance, device,
			vk::fence_create_flags{ vk::fence_create_flag::signaled }
		);

		on_scope_exit destroy_submit_fence = [&] {
			vk::destroy_fence(instance, device, submit_fence);
			print::out("submit fence is destroyed\n");
		};

		print::out("submit fence is created\n");

		print::out.flush();

		while(!window->should_close()) {
			glfw_instance.poll_events();

			float t = 1.0;

			if(game_state == game_state_t::animating) {
				auto[s, ns] = posix::monolitic_clock.secods_and_nanoseconds();
				nuint diff =
					(s - animation_begin_time.seconds) * 1000 +
					(int64(ns) - int64(animation_begin_time.nanoseconds))
						/ (1000 * 1000);

				if(diff > animation_ms) {
					game_state = game_state_t::waiting_input;
					movement_table = movement_table_t{};
				}
				else {
					t = float(diff) / float(animation_ms);
				}
			}

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

			struct tile_position_and_size_t {
				math::vector<float, 3> position;
				float size;
				uint32 number;
				uint32 padding[3];

				tile_position_and_size_t() = default;

				tile_position_and_size_t(
					math::vector<float, 3> position,
					float size,
					uint32 number
				) :
					position { position },
					size { size },
					number { number }
				{}
			};

			tile_position_and_size_t positions_raw[
				65536 / sizeof(tile_position_and_size_t)
			];
			list positions_list {
				span {
					(storage<tile_position_and_size_t>*) positions_raw,
					table_rows * table_rows * 2
				}
			};

			struct positions_and_letters_t {
				math::vector<float, 3> position;
				uint32 letter;
				float width;
				uint64 padding;

				positions_and_letters_t() = default;

				positions_and_letters_t(
					math::vector<float, 3> position,
					uint32 letter,
					float width
				) : position { position },
					letter { letter },
					width { width }
				{}
			};

			positions_and_letters_t digits_and_letters_positions_raw[
				65536 / sizeof(positions_and_letters_t)
			];
			list digits_and_letters_positions_list {
				span {
					(storage<positions_and_letters_t>*)
					digits_and_letters_positions_raw,
					nuint(65536 / sizeof(positions_and_letters_t))
				}
			};

			math::vector extent_f {
				(float) extent[0], (float) extent[1]
			};

			float table_size =
				numbers {
					extent_f[0], extent_f[1]
				}.min() / 1.1F;

			float tile_size = table_size / float(table_rows) / 1.1F;

			auto& current_tiles =
				game_state == game_state_t::animating ?
				prev_table.tiles :
				table.tiles;

			for(nuint y = 0; y < table_rows; ++y) {
				for(nuint x = 0; x < table_rows; ++x) {
					movement_t movement = movement_table.tiles[y][x];
					direction_t movement_direction
						= movement.get_same_as<direction_t>();
					nuint movement_distance = movement.get_same_as<nuint>();

					math::vector p0
						= math::vector { float(x), float(y) };

					math::vector p1 = p0 +
						math::vector {
							movement_direction.x, movement_direction.y
						} * t * float(movement_distance);

					math::vector tile_position_0 =
						extent_f / 2.0F +
						((p0 + 0.5F) / float(table_rows) - 0.5) * table_size;

					math::vector tile_position_1 =
						extent_f / 2.0F +
						((p1 + 0.5F) / float(table_rows) - 0.5) * table_size;

					float z = 0.9 - float(movement_distance) / 100.0F;

					positions_list.emplace_back(
						math::vector<float, 3> {
							tile_position_0[0], tile_position_0[1], 1.0F
						},
						tile_size,
						0
					);

					if(current_tiles[y][x] == 0) continue;

					positions_list.emplace_back(
						math::vector<float, 3> {
							tile_position_1[0], tile_position_1[1], z
						},
						tile_size,
						current_tiles[y][x]
					);

					nuint digits_count = 0;
					number { current_tiles[y][x] }.for_each_digit(
						number_base { 10 }, [&](auto) {
							++digits_count;
						}
					);

					nuint digit_index = 0;
					number { current_tiles[y][x] }.for_each_digit(
						number_base { 10 },
						[&] (nuint digit) {

							float full_digit_width = tile_size / 3.0F;
							float digit_width = full_digit_width;

							math::vector digit_position =
								tile_position_1 + math::vector {
									full_digit_width * (
										- float(digits_count) / 2.0F +
										(0.5F + digit_index)
									),
									0.0F
								};

							digits_and_letters_positions_list.emplace_back(
								math::vector<float, 3> {
									digit_position[0], digit_position[1], z
								},
								uint32('0' + digit),
								digit_width
							);
							++digit_index;
						}
					);
				}
			}

			vk::cmd_update_buffer(instance, device, command_buffer,
				tile_uniform_buffer, vk::memory_size {
					positions_list.size() * sizeof(tile_position_and_size_t)
				},
				(void*) positions_raw
			);
			vk::cmd_pipeline_barrier(instance, device, command_buffer,
				vk::src_stages { vk::pipeline_stage::transfer },
				vk::dst_stages { vk::pipeline_stage::vertex_shader },
				array {
					vk::memory_barrier {
						vk::src_access { vk::access::memory_write },
						vk::dst_access { vk::access::uniform_read }
					}
				}
			);
			vk::cmd_begin_render_pass(instance, device, command_buffer,
				tile_render_pass,
				framebuffers[image_index],
				vk::render_area { extent },
				array {
					vk::clear_value { .depth_stencil =
						vk::clear_depth_stencil_value { .depth = 1.0F }
					},
					vk::clear_value {
						vk::clear_color_value { 0.0F, 0.0F, 0.0F, 0.0F }
					}
				}
			);
			vk::cmd_bind_pipeline(instance, device, command_buffer,
				tile_pipeline, vk::pipeline_bind_point::graphics
			);
			vk::cmd_bind_descriptor_sets(instance, device, command_buffer,
				vk::pipeline_bind_point::graphics,
				tile_pipeline_layout,
				array { tile_descriptor_set }
			);
			vk::cmd_set_scissor(instance, device, command_buffer, extent);
			vk::cmd_set_viewport(instance, device, command_buffer, extent);

			vk::cmd_push_constants(
				instance, device, command_buffer,
				tile_pipeline_layout,
				vk::push_constant_range {
					vk::shader_stages {
						vk::shader_stage::vertex
					},
					vk::size { 2 * sizeof(uint32) }
				},
				(void*) &extent
			);
			vk::cmd_draw(instance, device, command_buffer,
				vk::vertex_count { 3 * 2 * (uint32) positions_list.size() }
			);
			vk::cmd_end_render_pass(instance, device, command_buffer);

			vk::cmd_update_buffer(instance, device, command_buffer,
				digits_and_letters_uniform_buffer, vk::memory_size {
					sizeof(positions_and_letters_t) *
					digits_and_letters_positions_list.size()
				},
				(void*) digits_and_letters_positions_raw
			);
			vk::cmd_pipeline_barrier(instance, device, command_buffer,
				vk::src_stages { vk::pipeline_stage::transfer },
				vk::dst_stages { vk::pipeline_stage::vertex_shader },
				array {
					vk::memory_barrier {
						vk::src_access { vk::access::memory_write },
						vk::dst_access { vk::access::uniform_read }
					}
				}
			);
			vk::cmd_begin_render_pass(instance, device, command_buffer,
				digits_and_letters_render_pass,
				framebuffers[image_index],
				vk::render_area { extent }
			);
			vk::cmd_bind_pipeline(instance, device, command_buffer,
				digits_and_letters_pipeline, vk::pipeline_bind_point::graphics
			);
			vk::cmd_bind_descriptor_sets(instance, device, command_buffer,
				vk::pipeline_bind_point::graphics,
				digits_and_letters_pipeline_layout,
				array { digits_and_letters_descriptor_set }
			);
			vk::cmd_set_scissor(instance, device, command_buffer, extent);
			vk::cmd_set_viewport(instance, device, command_buffer, extent);
			vk::cmd_draw(instance, device, command_buffer,
				vk::vertex_count {
					(uint32) digits_and_letters_positions_list.size() * 3 * 2
				}
			);
			vk::cmd_end_render_pass(instance, device, command_buffer);

			vk::end_command_buffer(instance, device, command_buffer);

			vk::queue_submit(
				instance, device, queue, command_buffer,
				vk::wait_semaphore { acquire_semaphore },
				vk::signal_semaphore { submit_semaphore },
				vk::signal_fence { submit_fence }
			);

			vk::result present_result = vk::try_queue_present(
				instance, device, queue,
				swapchain, image_index,
				vk::wait_semaphore { submit_semaphore }
			);

			if(should_update_swapchain(present_result)) {
				break;
			}
		}

		vk::device_wait_idle(instance, device);
	}
}