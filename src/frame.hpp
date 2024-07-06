#pragma once

#include <numbers.hpp>
#include <on_scope_exit.hpp>
#include <vk/swapchain.hpp>
#include <vk/surface.hpp>
#include <vk/image.hpp>
#include <vk/device_memory.hpp>
#include <vk/device.hpp>
#include <vk/framebuffer.hpp>
#include <vk/image_view.hpp>
#include <vk/render_pass.hpp>
#include <vk/command_buffer.hpp>
#include <vk/fence.hpp>
#include <vk/queue.hpp>
#include <vk/semaphore.hpp>
#include <vk/physical_device.hpp>
#include <glfw/window.hpp>
#include <glfw/instance.hpp>
#include <print/print.hpp>
#include <posix/abort.hpp>
#include "./glfw.hpp"
#include "./state.hpp"


inline void frame(
	handle<vk::instance> instance,
	handle<vk::physical_device> physical_device,
	handle<vk::device> device,
	handle<vk::surface> surface,
	vk::surface_format surface_format,
	handle<vk::command_pool> command_pool,
	handle<vk::queue> queue,

	handle<vk::render_pass> tile_render_pass,
	handle<vk::pipeline> tile_pipeline,
	handle<vk::pipeline_layout> tile_pipeline_layout,
	handle<vk::buffer> tile_uniform_buffer,
	handle<vk::descriptor_set> tile_descriptor_set,

	handle<vk::render_pass> digits_and_letters_render_pass,
	handle<vk::pipeline> digits_and_letters_pipeline,
	handle<vk::pipeline_layout> digits_and_letters_pipeline_layout,
	handle<vk::buffer> digits_and_letters_uniform_buffer,
	handle<vk::descriptor_set> digits_and_letters_descriptor_set
) {
	handle<vk::swapchain> swapchain{};
	on_scope_exit destroy_swapchain = [&] {
		if (swapchain.is_valid()) {
			vk::destroy_swapchain(instance, device, swapchain);
		}
		print::out("swapchain is destroyed\n");
	};

	print::out.flush();

	while (!window->should_close()) {
		handle<vk::swapchain> prev_swapchain = swapchain;
		auto window_size = window->get_size().cast<uint32>();

		vk::surface_capabilities surface_caps
			= vk::get_physical_device_surface_capabilities(
				instance, physical_device, surface
			);

		vk::extent<2> extent = surface_caps.current_extent;

		if (extent == 0u) { // on windows, window is minimised. TODO
			glfw_instance.poll_events();
			continue;
		}
		if (extent == -1u) { // on linux, wayland
			extent = window_size;
			extent = extent.clamp(
				surface_caps.min_image_extent,
				surface_caps.max_image_extent
			);
		}

		swapchain = vk::create_swapchain(
			instance, device, surface,
			vk::min_image_count {
				surface_caps.max_image_count != 0 ?
					number { 2u }.clamp(
						surface_caps.min_image_count,
						surface_caps.max_image_count
					) :
					numbers { 2u, (uint32) surface_caps.min_image_count }.max()
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

		if (prev_swapchain.is_valid()) {
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
		for (nuint i = 0; i < swapchain_images_count; ++i) {
			image_views[i] = vk::create_image_view(
				instance, device, swapchain_images[i],
				surface_format.format, vk::image_view_type::two_d
			);
		}
		on_scope_exit destroy_image_views = [&] {
			for (handle<vk::image_view> image_view : image_views) {
				vk::destroy_image_view(instance, device, image_view);
			}
			print::out("swapchain's image views are freed\n");
		};
		print::out("swapchain's image views are created\n");

		handle<vk::image> depth_images_raw[swapchain_images_count];
		span depth_images { depth_images_raw, swapchain_images_count };

		for (nuint i = 0; i < swapchain_images_count; ++i) {
			depth_images[i] = vk::create_image(instance, device,
				vk::format::d32_sfloat,
				extent,
				vk::image_tiling::optimal,
				vk::image_usages { vk::image_usage::depth_stencil_attachment }
			);
		}
		on_scope_exit destroy_depth_images { [&] {
			for (handle<vk::image> depth_image : depth_images) {
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

		for (nuint i = 0; i < swapchain_images_count; ++i) {
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

		for (nuint i = 0; i < swapchain_images_count; ++i) {
			depth_image_views[i] = vk::create_image_view(instance, device,
				depth_images[i], vk::format::d32_sfloat,
				vk::image_view_type::two_d,
				vk::image_subresource_range {
					vk::image_aspects { vk::image_aspect::depth }
				}
			);
		}
		on_scope_exit destroy_depth_image_views { [&] {
			for (handle<vk::image_view> depth_image_view : depth_image_views) {
				vk::destroy_image_view(instance, device, depth_image_view);
			}
			print::out("depth image views are destroyed\n");
		}};
		print::out("depth image views are created\n");

		handle<vk::framebuffer> framebuffers_raw[swapchain_images_count];
		span framebuffers { framebuffers_raw, swapchain_images_count };
		for (nuint i = 0; i < swapchain_images_count; ++i) {
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
			for (handle<vk::framebuffer> framebuffer : framebuffers) {
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
		for (nuint i = 0; i < swapchain_images_count; ++i) {
			fences[i] = vk::create_fence(instance, device);
		}
		on_scope_exit destroy_fences = [&] {
			for (handle<vk::fence> fence : fences) {
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

		while (!window->should_close()) {
			glfw_instance.poll_events();
			if (window->get_size().cast<uint32>() != window_size) {
				break;
			}

			float t = 1.0;

			if (game_state == game_state::animating) {
				auto new_time = posix::get_ticks();
				nuint diff_ms = (new_time - animation_begin_time) * 1000 / posix::ticks_per_second;

				if (diff_ms > animation_ms) {
					game_state = game_state::waiting_input;
					movement_table = movement_table_t{};
				}
				else {
					t = float(diff_ms) / float(animation_ms);
				}
			}

			
			vk::wait_for_fence(instance, device, submit_fence);
			vk::reset_fence(instance, device, submit_fence);

			vk::expected<vk::image_index> acquire_result
				= vk::try_acquire_next_image(
					instance, device, swapchain,
					vk::signal_semaphore { acquire_semaphore }
				);
			
			auto should_update_swapchain = [&](vk::result result) {
				if (result.success()) return false;
				if (result.suboptimal() || result.out_of_date()) return true;
				posix::abort();
			};

			if (
				acquire_result.is_unexpected() &&
				should_update_swapchain(acquire_result.get_unexpected())
			) {
				print::out("swapchain is suboptimal or out of date\n");
				break;
			}

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

			list positions_list {
				array<
					tile_position_and_size_t,
					table_rows * table_rows * 2
				>{}
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

			list digits_and_letters_positions_list {
				array<
					positions_and_letters_t,
					65536 / sizeof(positions_and_letters_t)
				>{}
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
				game_state == game_state::animating ?
				prev_table.tiles :
				table.tiles;

			for (nuint y = 0; y < table_rows; ++y) {
				for (nuint x = 0; x < table_rows; ++x) {
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

					if (current_tiles[y][x] == 0) continue;

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

			vk::image_index image_index = acquire_result.get_expected();

			handle<vk::command_buffer> command_buffer
				= command_buffers[image_index];

			vk::begin_command_buffer(
				instance, device, command_buffer,
				vk::command_buffer_usages {
					vk::command_buffer_usage::one_time_submit
				}
			);

			vk::cmd_update_buffer(instance, device, command_buffer,
				tile_uniform_buffer, vk::memory_size {
					positions_list.size() * sizeof(tile_position_and_size_t)
				},
				(void*) positions_list.iterator()
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
				(void*) digits_and_letters_positions_list.iterator()
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
				vk::pipeline_stages {
					vk::pipeline_stage::color_attachment_output
				},
				vk::signal_semaphore { submit_semaphore },
				vk::signal_fence { submit_fence }
			);

			vk::result present_result = vk::try_queue_present(
				instance, device, queue,
				swapchain, image_index,
				vk::wait_semaphore { submit_semaphore }
			);

			if (should_update_swapchain(present_result)) {
				break;
			}
		}

		vk::device_wait_idle(instance, device);
	}
}