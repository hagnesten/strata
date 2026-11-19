#pragma once

#include "studio/widgets/widgets.hh"
#include "storage/record/record.hh"
#include "base/arena/arena.hh"

#define GRID_ROW_LIMIT 200000
#define GRID_MEASURE_ROWS 400
#define GRID_CELL_CHARS 400

namespace studio {

	struct grid_column {

		const char* name;
		float width;
		bool numeric;

	};

	struct grid_data {

		base::arena memory;
		grid_column* columns;
		uint32_t column_count;
		const char** cells;
		uint8_t* kinds;
		uint32_t cell_count;
		uint32_t cell_room;
		uint32_t kind_room;
		uint32_t rows;
		bool truncated;
		bool loaded;
		float scroll_x;
		float scroll_y;
		int32_t selected_row;
		int32_t selected_column;
		int32_t resizing;
		float resize_origin;
		float resize_width;
		uint64_t id;

	};

	void clear_grid ( grid_data& grid );

	bool grid_columns ( void* context, const char* const* names, uint32_t count );

	bool grid_row ( void* context, const storage::value* values, uint32_t count );

	void finish_grid ( grid_data& grid, ui::renderer& draw );

	void draw_grid ( ui_context& ui, grid_data& grid, const ui::rect& area, const char* empty );

	const char* grid_cell ( const grid_data& grid, uint32_t row, uint32_t column );

}
