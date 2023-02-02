#pragma once

#include <posix/random.hpp>

#include <array.hpp>
#include <storage.hpp>
#include <list.hpp>

static constexpr nuint table_rows = 4;

enum class direction {
	up, down, left, right
};

static struct table_t {
	array<array<uint32, table_rows>, table_rows> tiles;

	inline bool try_put_random_value();

	template<direction Dir>
	bool try_move();

	template<direction Dir>
	auto rotated_tiles_view();

} table;

/*
 default: (up)
 [0][0] [0][1] [0][2] **  **
 [1][0] [1][1] [1][2] **  **
   **     **     **   *
   **     **     **       *
*/
template<direction Dir>
auto table_t::rotated_tiles_view() {
	if constexpr(Dir == direction::up) {
		return tiles.transform_view([](auto& e) -> auto& { return e; });
	}
	else if constexpr(Dir == direction::down) {
		return tiles.reverse_view();
	}
	else {
		return tiles.transform_view([&](auto& y_value) {
			nuint y_index = &y_value - tiles.iterator();
			return y_value.transform_view(
				[&, y_index = y_index](uint32& x_value) -> uint32& {
					nuint x_index = &x_value - y_value.iterator();
					if constexpr(Dir == direction::left) {
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
	nuint tile_values_raw_size = table_rows * table_rows;
	storage<uint32*> tile_values_raw[tile_values_raw_size];

	list tile_values{ span{ tile_values_raw, tile_values_raw_size } };

	for(nuint y = 0; y < table.tiles.size(); ++y) {
		for(nuint x = 0; x < table.tiles[y].size(); ++x) {
			uint32& value = table.tiles[y][x];
			if(value == 0) {
				tile_values.emplace_back(&value);
			}
		}
	}

	if(tile_values.size() == 0) return false;

	nuint rand_index = posix::rand() % tile_values.size();
	uint32 rand_value = (posix::rand() % 2 + 1) * 2;
	*tile_values[rand_index] = rand_value;

	return true;
}

template<direction Dir>
bool table_t::try_move() {
	auto table = rotated_tiles_view<Dir>();
	bool moved = false;

	for(nuint y = 1; y < table.size(); ++y) {
		for(nuint x = 0; x < table[y].size(); ++x) {
			if(table[y][x] == 0) continue;

			nuint empty_y = -1;

			for(nuint offset = 1; offset <= y; ++offset) {
				if(table[y - offset][x] == 0) {
					empty_y = y - offset;
				}
				else if(table[y - offset][x] == table[y][x]) {
					table[y - offset][x] *= 2;
					table[y][x] = 0;
					moved = true;
					continue;
				}
				else {
					break;
				}
			}

			if(empty_y != nuint(-1)) {
				table[empty_y][x] = table[y][x];
				table[y][x] = 0;
				moved = true;
			}
		}
	};

	return moved;
};