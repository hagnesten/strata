#include "studio/grid/grid.hh"
#include "engine/database/database.hh"
#include "base/clipboard/clipboard.hh"

#include <windows.h>

namespace studio {

	void clear_grid ( grid_data& grid ) {

		grid.memory.reset ( );
		grid.columns = nullptr;
		grid.column_count = 0;
		grid.cells = nullptr;
		grid.kinds = nullptr;
		grid.cell_count = 0;
		grid.cell_room = 0;
		grid.kind_room = 0;
		grid.rows = 0;
		grid.truncated = false;
		grid.loaded = false;
		grid.scroll_x = 0;
		grid.scroll_y = 0;
		grid.selected_row = -1;
		grid.selected_column = -1;
		grid.resizing = -1;

	}

	bool grid_columns ( void* context, const char* const* names, uint32_t count ) {

		grid_data& grid = *static_cast < grid_data* > ( context );
		uint64_t id = grid.id;

		clear_grid ( grid );
		grid.id = id;
		grid.loaded = true;
		grid.columns = grid.memory.make < grid_column > ( count + 1 );

		if ( !grid.columns )
			return false;

		grid.column_count = count;

		for ( uint32_t c = 0; c < count; c++ )
			grid.columns [ c ].name = grid.memory.keep ( names [ c ], base::length ( names [ c ] ) );

		return true;

	}

	bool grid_row ( void* context, const storage::value* values, uint32_t count ) {

		grid_data& grid = *static_cast < grid_data* > ( context );

		if ( grid.rows >= GRID_ROW_LIMIT ) {

			grid.truncated = true;

			return false;

		}

		for ( uint32_t c = 0; c < count && c < grid.column_count; c++ ) {

			base::text cell;
			const storage::value& value = values [ c ];

			if ( value.kind == storage::VALUE_TEXT && value.size > GRID_CELL_CHARS ) {

				size_t cut = base::skip_codes ( value.bytes, value.size, GRID_CELL_CHARS );

				cell.add ( reinterpret_cast < const char* > ( value.bytes ), cut ).add ( "\xE2\x80\xA6" );

			} else if ( value.kind == storage::VALUE_BLOB && value.size > GRID_CELL_CHARS / 2 ) {

				storage::value shown = value;

				shown.size = GRID_CELL_CHARS / 2;
				engine::describe_value ( cell, shown );
				cell.add ( "\xE2\x80\xA6" );

			} else
				engine::describe_value ( cell, value );

			for ( size_t i = 0; i < cell.size; i++ )
				if ( cell.data [ i ] == '\n' || cell.data [ i ] == '\r' || cell.data [ i ] == '\t' )
					cell.data [ i ] = ' ';

			const char* kept = grid.memory.keep ( cell.value ( ), cell.size );
			uint8_t kind = value.kind;
			uint32_t kinds = grid.cell_count;

			if ( !kept || !base::push ( grid.memory, grid.cells, grid.cell_count, grid.cell_room, kept ) || !base::push ( grid.memory, grid.kinds, kinds, grid.kind_room, kind ) )
				return false;

			if ( value.kind == storage::VALUE_INTEGER || value.kind == storage::VALUE_REAL )
				grid.columns [ c ].numeric = true;

		}

		grid.rows++;

		return true;

	}

	const char* grid_cell ( const grid_data& grid, uint32_t row, uint32_t column ) {

		return grid.cells [ static_cast < size_t > ( row ) * grid.column_count + column ];

	}

	void finish_grid ( grid_data& grid, ui::renderer& draw ) {

		for ( uint32_t c = 0; c < grid.column_count; c++ ) {

			float widest = ui::measure ( draw, ui::FONT_STRONG, grid.columns [ c ].name ) + 28;
			uint32_t rows = grid.rows < GRID_MEASURE_ROWS ? grid.rows : GRID_MEASURE_ROWS;

			for ( uint32_t r = 0; r < rows; r++ ) {

				const char* cell = grid_cell ( grid, r, c );
				float width = static_cast < float > ( base::count_codes ( reinterpret_cast < const uint8_t* > ( cell ), base::length ( cell ) ) ) * draw.ui_advance + 26;

				widest = base::max ( widest, width );

			}

			grid.columns [ c ].width = clamp ( widest, 64, 380 );

		}

	}

	static float content_width ( const grid_data& grid ) {

		float total = 0;

		for ( uint32_t c = 0; c < grid.column_count; c++ )
			total += grid.columns [ c ].width;

		return total;

	}

	static void copy_selection ( grid_data& grid ) {

		if ( grid.selected_row < 0 || grid.selected_row >= static_cast < int32_t > ( grid.rows ) )
			return;

		base::text out;

		if ( grid.selected_column >= 0 && grid.selected_column < static_cast < int32_t > ( grid.column_count ) )
			out.add ( grid_cell ( grid, static_cast < uint32_t > ( grid.selected_row ), static_cast < uint32_t > ( grid.selected_column ) ) );
		else {

			for ( uint32_t c = 0; c < grid.column_count; c++ ) {

				if ( c )
					out.add ( '\t' );

				out.add ( grid_cell ( grid, static_cast < uint32_t > ( grid.selected_row ), c ) );

			}

		}

		base::copy_to_clipboard ( out.value ( ), out.size );

	}

	void draw_grid ( ui_context& ui, grid_data& grid, const ui::rect& area, const char* empty ) {

		ui::renderer& draw = *ui.draw;
		input_state& input = *ui.input;

		ui::fill ( draw, area, ui::rgb ( THEME_BACKGROUND ) );

		if ( !grid.loaded || !grid.column_count ) {

			ui::text ( draw, ui::FONT_UI, empty, area, ui::rgb ( THEME_FAINT ), ui::ALIGN_CENTER );

			return;

		}

		base::text digits;

		digits.number ( grid.rows ? grid.rows : 1 );

		float gutter = static_cast < float > ( digits.size ) * draw.code_small_advance + 22;
		float view_w = area.w - gutter - LAYOUT_SCROLLBAR;
		float view_h = area.h - LAYOUT_HEADER - LAYOUT_SCROLLBAR;
		float total_w = content_width ( grid );
		float total_h = static_cast < float > ( grid.rows ) * LAYOUT_ROW;
		ui::rect body = { area.x + gutter, area.y + LAYOUT_HEADER, view_w, view_h };

		if ( ui::inside ( area, input.x, input.y ) && input.wheel != 0 && ( !ui.active || ui.active == grid.id ) ) {

			if ( input.shift )
				grid.scroll_x -= input.wheel * 60;
			else
				grid.scroll_y -= input.wheel * LAYOUT_ROW * 3;

		}

		if ( ui.focus == grid.id && input.key_count ) {

			int32_t page = static_cast < int32_t > ( view_h / LAYOUT_ROW ) - 1;
			int32_t row = grid.selected_row;

			if ( key_hit ( ui, VK_DOWN ) )
				row++;

			if ( key_hit ( ui, VK_UP ) )
				row--;

			if ( key_hit ( ui, VK_NEXT ) )
				row += page;

			if ( key_hit ( ui, VK_PRIOR ) )
				row -= page;

			if ( key_hit ( ui, VK_HOME ) && input.ctrl )
				row = 0;

			if ( key_hit ( ui, VK_END ) && input.ctrl )
				row = static_cast < int32_t > ( grid.rows ) - 1;

			if ( key_hit ( ui, VK_LEFT ) && grid.selected_column > 0 )
				grid.selected_column--;

			if ( key_hit ( ui, VK_RIGHT ) && grid.selected_column + 1 < static_cast < int32_t > ( grid.column_count ) )
				grid.selected_column++;

			if ( row != grid.selected_row && grid.rows ) {

				grid.selected_row = static_cast < int32_t > ( clamp ( static_cast < float > ( row ), 0, static_cast < float > ( grid.rows - 1 ) ) );

				float top = static_cast < float > ( grid.selected_row ) * LAYOUT_ROW;

				if ( top < grid.scroll_y )
					grid.scroll_y = top;

				if ( top + LAYOUT_ROW > grid.scroll_y + view_h )
					grid.scroll_y = top + LAYOUT_ROW - view_h;

			}

			if ( input.ctrl && key_hit ( ui, 'C' ) )
				copy_selection ( grid );

		}

		grid.scroll_x = clamp ( grid.scroll_x, 0, total_w - view_w );
		grid.scroll_y = clamp ( grid.scroll_y, 0, total_h - view_h );

		uint32_t first = static_cast < uint32_t > ( grid.scroll_y / LAYOUT_ROW );
		uint32_t visible = static_cast < uint32_t > ( view_h / LAYOUT_ROW ) + 2;
		float offset_y = -( grid.scroll_y - static_cast < float > ( first ) * LAYOUT_ROW );

		ui::push_clip ( draw, { area.x, body.y, area.w, view_h } );

		for ( uint32_t i = 0; i < visible && first + i < grid.rows; i++ ) {

			uint32_t r = first + i;
			float y = body.y + offset_y + static_cast < float > ( i ) * LAYOUT_ROW;
			ui::rect row_area = { area.x, y, gutter + view_w, LAYOUT_ROW };
			bool selected = static_cast < int32_t > ( r ) == grid.selected_row;
			bool hover = ui::inside ( row_area, input.x, input.y ) && ui::inside ( body, input.x, input.y ) && !ui.active;

			if ( selected )
				ui::fill ( draw, row_area, ui::rgb ( THEME_SELECTED ) );
			else if ( hover )
				ui::fill ( draw, row_area, ui::rgb ( THEME_HOVER ) );
			else if ( r & 1 )
				ui::fill ( draw, row_area, ui::rgb ( THEME_STRIPE ) );

			if ( selected )
				ui::fill ( draw, { area.x, y + 3, 2, LAYOUT_ROW - 6 }, ui::rgb ( THEME_ACCENT ) );

			base::text number;

			number.number ( r + 1 );
			ui::text ( draw, ui::FONT_CODE_SMALL, number.value ( ), number.size, { area.x, y, gutter - 10, LAYOUT_ROW }, ui::rgb ( THEME_FAINT ), ui::ALIGN_RIGHT );

		}

		ui::push_clip ( draw, body );

		float x = body.x - grid.scroll_x;

		for ( uint32_t c = 0; c < grid.column_count; c++ ) {

			float width = grid.columns [ c ].width;

			if ( x + width >= body.x && x <= body.x + body.w ) {

				for ( uint32_t i = 0; i < visible && first + i < grid.rows; i++ ) {

					uint32_t r = first + i;
					float y = body.y + offset_y + static_cast < float > ( i ) * LAYOUT_ROW;
					size_t at = static_cast < size_t > ( r ) * grid.column_count + c;
					uint8_t kind = grid.kinds [ at ];
					ui::rect cell = { x + 10, y, width - 20, LAYOUT_ROW };
					bool numeric = kind == storage::VALUE_INTEGER || kind == storage::VALUE_REAL;
					ui::color shade = kind == storage::VALUE_NULL ? ui::rgb ( THEME_FAINT ) : kind == storage::VALUE_BLOB ? ui::rgb ( THEME_DIM ) : numeric ? ui::rgb ( THEME_CELL_NUMBER ) : ui::rgb ( THEME_TEXT );

					if ( static_cast < int32_t > ( r ) == grid.selected_row && static_cast < int32_t > ( c ) == grid.selected_column )
						ui::outline ( draw, { x + 1, y + 1, width - 2, LAYOUT_ROW - 2 }, ui::rgb ( THEME_ACCENT, 0.7f ) );

					ui::text ( draw, ui::FONT_UI, grid.cells [ at ], cell, shade, numeric ? ui::ALIGN_RIGHT : ui::ALIGN_LEFT );

				}

			}

			x += width;
			ui::line ( draw, x - 0.5f, body.y, x - 0.5f, base::min ( body.y + body.h, body.y + total_h - grid.scroll_y ), ui::rgb ( THEME_DIVIDER, 0.6f ) );

		}

		ui::pop_clip ( draw );
		ui::pop_clip ( draw );

		if ( ui::inside ( body, input.x, input.y ) && input.pressed && !ui.active ) {

			int32_t row = static_cast < int32_t > ( ( input.y - body.y + grid.scroll_y ) / LAYOUT_ROW );
			float column_x = body.x - grid.scroll_x;

			grid.selected_column = -1;

			for ( uint32_t c = 0; c < grid.column_count; c++ ) {

				if ( input.x >= column_x && input.x < column_x + grid.columns [ c ].width )
					grid.selected_column = static_cast < int32_t > ( c );

				column_x += grid.columns [ c ].width;

			}

			if ( row >= 0 && row < static_cast < int32_t > ( grid.rows ) )
				grid.selected_row = row;

			ui.focus = grid.id;
			ui.redraw = true;

		}

		if ( ui::inside ( { area.x, body.y, gutter, view_h }, input.x, input.y ) && input.pressed && !ui.active ) {

			int32_t row = static_cast < int32_t > ( ( input.y - body.y + grid.scroll_y ) / LAYOUT_ROW );

			if ( row >= 0 && row < static_cast < int32_t > ( grid.rows ) ) {

				grid.selected_row = row;
				grid.selected_column = -1;

			}

			ui.focus = grid.id;
			ui.redraw = true;

		}

		ui::rect header = { area.x, area.y, area.w, LAYOUT_HEADER };

		ui::fill ( draw, header, ui::rgb ( THEME_PANEL ) );
		ui::line ( draw, area.x, area.y + LAYOUT_HEADER - 0.5f, area.x + area.w, area.y + LAYOUT_HEADER - 0.5f, ui::rgb ( THEME_DIVIDER ) );
		ui::line ( draw, area.x + gutter - 0.5f, area.y, area.x + gutter - 0.5f, base::min ( area.y + area.h - LAYOUT_SCROLLBAR, body.y + total_h - grid.scroll_y ), ui::rgb ( THEME_DIVIDER ) );
		ui::push_clip ( draw, { body.x, area.y, view_w, LAYOUT_HEADER } );

		x = body.x - grid.scroll_x;

		for ( uint32_t c = 0; c < grid.column_count; c++ ) {

			float width = grid.columns [ c ].width;
			ui::rect edge = { x + width - 4, area.y, 8, LAYOUT_HEADER };
			uint64_t edge_id = grid.id ^ ( 0x5000 + c );

			ui::text ( draw, ui::FONT_STRONG, grid.columns [ c ].name, { x + 10, area.y, width - 20, LAYOUT_HEADER }, ui::rgb ( static_cast < int32_t > ( c ) == grid.selected_column ? THEME_ACCENT_SOFT : THEME_DIM ), grid.columns [ c ].numeric ? ui::ALIGN_RIGHT : ui::ALIGN_LEFT );
			ui::line ( draw, x + width - 0.5f, area.y + 7, x + width - 0.5f, area.y + LAYOUT_HEADER - 7, ui::rgb ( THEME_BORDER ) );

			if ( ui::inside ( edge, input.x, input.y ) && ( !ui.active || ui.active == edge_id ) ) {

				ui.cursor = CURSOR_SIZE_WE;

				if ( input.pressed ) {

					ui.active = edge_id;
					grid.resizing = static_cast < int32_t > ( c );
					grid.resize_origin = input.x;
					grid.resize_width = width;

				}

			}

			if ( ui.active == edge_id && input.down && grid.resizing == static_cast < int32_t > ( c ) ) {

				ui.cursor = CURSOR_SIZE_WE;
				grid.columns [ c ].width = clamp ( grid.resize_width + input.x - grid.resize_origin, 40, 1200 );
				ui.redraw = true;

			}

			x += width;

		}

		ui::pop_clip ( draw );

		grid.scroll_y = scrollbar ( ui, grid.id ^ 0x77, { area.x + area.w - LAYOUT_SCROLLBAR, body.y, LAYOUT_SCROLLBAR, view_h }, total_h, view_h, grid.scroll_y, true );
		grid.scroll_x = scrollbar ( ui, grid.id ^ 0x78, { body.x, area.y + area.h - LAYOUT_SCROLLBAR, view_w, LAYOUT_SCROLLBAR }, total_w, view_w, grid.scroll_x, false );

	}

}
