#pragma once

#include <posix/time.hpp>
#include "./table.hpp"

static enum class game_state {
	waiting_input, animating
} game_state = game_state::waiting_input;


static posix::ticks_t animation_begin_time{};
static constexpr nuint animation_ms = 100;
static table_t prev_table{};
static movement_table_t movement_table;