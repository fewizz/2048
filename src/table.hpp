#pragma once

#include <posix/random.hpp>

#include <array.hpp>
#include <storage.hpp>
#include <list.hpp>

static constexpr nuint table_rows = 4;

struct direction_t {
	uint8 value = -1;
	float x = 0.0; float y = 0.0;

	constexpr direction_t() {}
	constexpr direction_t(uint8 value, float x, float y) :
		value { value }, x { x }, y { y }
	{}

	constexpr bool operator == (direction_t d) const {
		return d.value == value;
	}
};

static constexpr direction_t
	invalid{},
	up    { 0,  0.0, -1.0 },
	down  { 1,  0.0,  1.0 },
	left  { 2, -1.0,  0.0 },
	right { 3,  1.0,  0.0 };

using movement_t = tuple<direction_t, nuint>;

struct movement_table_t {
	array<array<movement_t, table_rows>, table_rows> tiles;

	movement_table_t() {}
	movement_table_t(const movement_table_t& other) : tiles{ other.tiles } {}
	movement_table_t& operator = (const movement_table_t& other) {
		tiles = other.tiles;
		return *this;
	}
};

static struct table_t {
	array<array<uint32, table_rows>, table_rows> tiles;

	inline bool try_put_random_value();

	template<direction_t Dir>
	optional<::movement_table_t> try_move();

} table;

/*
 default: (up)
 [0][0] [0][1] [0][2] **  **
 [1][0] [1][1] [1][2] **  **
   **     **     **   *
   **     **     **       *
*/
template<direction_t Dir>
auto rotated_view(auto& tiles) {
	if constexpr(Dir == up) {
		return tiles.transform_view([](auto& e) -> auto& { return e; });
	}
	else if constexpr(Dir == down) {
		return tiles.reverse_view();
	}
	else {
		return tiles.transform_view([&](auto& y_value) {
			nuint y_index = &y_value - tiles.iterator();
			return y_value.transform_view(
				[&, y_index = y_index](auto& x_value) -> auto& {
					nuint x_index = &x_value - y_value.iterator();
					if constexpr(Dir == left) {
						return tiles[x_index][y_index];
					}
					else {
						return tiles[x_index].reverse_view()[y_index];
					}
				}
			);
		});
	}
}

bool table_t::try_put_random_value() {
	list tile_values {
		array<uint32*, table_rows * table_rows>{}
	};

	for (nuint y = 0; y < table.tiles.size(); ++y) {
		for (nuint x = 0; x < table.tiles[y].size(); ++x) {
			uint32& value = table.tiles[y][x];
			if (value == 0) {
				tile_values.emplace_back(&value);
			}
		}
	}

	if (tile_values.size() == 0) return false;

	nuint rand_index = posix::rand() % tile_values.size();
	uint32 rand_value = (posix::rand() % 2 + 1) * 2;
	*tile_values[rand_index] = rand_value;

	return true;
}

template<direction_t Dir>
optional<movement_table_t> table_t::try_move() {
	auto table = rotated_view<Dir>(tiles);
	bool moved = false;

	movement_table_t movement_table{};
	auto movements = rotated_view<Dir>(movement_table.tiles);

	for (nuint x = 0; x < table_rows; ++x) {
		nuint min_replacable_y = 0;
		for (nuint y = 1; y < table_rows; ++y) {
			if (table[y][x] == 0) continue;

			nuint new_y = y;

			nuint free_y = -1;

			for (nuint offset = 1; offset <= y; ++offset) {
				if (table[y - offset][x] == 0) {
					free_y = y - offset;
				}
				else {
					break;
				}
			}

			if (free_y != nuint(-1)) {
				new_y = free_y;
				table[new_y][x] = table[y][x];
				table[y][x] = 0;
			}

			if (
				new_y > min_replacable_y &&
				table[new_y][x] == table[new_y - 1][x]
			) {
				table[new_y - 1][x] *= 2.0;
				table[new_y][x] = 0;
				min_replacable_y = new_y;
				--new_y;
			}

			if (new_y != y) {
				moved = true;
				movements[y][x] = { Dir, y - new_y };
			}

			/*nuint empty_y = -1;

			for (nuint offset = 1; offset <= y; ++offset) {
				if (table[y - offset][x] == 0) {
					empty_y = y - offset;
				}
				else if (table[y - offset][x] == table[y][x]) {
					table[y - offset][x] *= 2;
					table[y][x] = 0;
					movements[y][x] = { Dir, offset };
					moved = true;
					continue;
				}
				else {
					break;
				}
			}

			if (empty_y != nuint(-1)) {
				table[empty_y][x] = table[y][x];
				table[y][x] = 0;
				movements[y][x] = { Dir, y - empty_y };
				moved = true;
			}*/
		}
	};

	if (!moved) {
		return {};
	}

	return { movement_table };
};