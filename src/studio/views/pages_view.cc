#include "studio/app/studio.hh"

namespace studio {

	static void draw_page_map ( studio_state& app, const ui::rect& area ) {

		ui_context& ui = app.ui;
		ui::renderer& draw = app.draw;
		input_state& input = app.input;
		ui::rect grid = { area.x + 20, area.y + 44, area.w - 40, area.h - 90 };
		float fit = 0;
		float pages = static_cast < float > ( app.page_total ? app.page_total : 1 );

		for ( float side = 32; side >= STUDIO_PAGE_CELL + 3; side -= 1 ) {

			float columns = static_cast < float > ( static_cast < uint32_t > ( grid.w / side ) );
			float rows_needed = static_cast < float > ( static_cast < uint32_t > ( ( pages + columns - 1 ) / ( columns > 0 ? columns : 1 ) ) );

			if ( rows_needed * side <= grid.h ) {

				fit = side;

				break;

			}

		}

		float pitch = fit > 0 ? fit : STUDIO_PAGE_CELL + 3;
		float gap = pitch > 20 ? 4 : 3;
		float cell = pitch - gap;
		uint32_t per_row = static_cast < uint32_t > ( grid.w / pitch );

		if ( !per_row )
			per_row = 1;

		base::text count;

		count.number ( app.page_total ).add ( app.page_total == 1 ? " page" : " pages" );
		caption ( ui, { area.x + 20, area.y + 8, area.w - 40, LAYOUT_CAPTION }, "File", count.value ( ) );

		uint32_t rows = ( app.page_total + per_row - 1 ) / per_row;
		float content = static_cast < float > ( rows ) * pitch;

		if ( ui::inside ( grid, input.x, input.y ) && input.wheel != 0 )
			app.pages_scroll -= input.wheel * pitch * 4;

		app.pages_scroll = clamp ( app.pages_scroll, 0, content - grid.h );
		ui::push_clip ( draw, grid );

		uint32_t first_row = static_cast < uint32_t > ( app.pages_scroll / pitch );

		for ( uint32_t r = first_row; r < rows && static_cast < float > ( r - first_row ) * pitch < grid.h + pitch; r++ ) {

			for ( uint32_t c = 0; c < per_row; c++ ) {

				uint32_t page = r * per_row + c + 1;

				if ( page > app.page_total )
					break;

				ui::rect box = { grid.x + static_cast < float > ( c ) * pitch, grid.y + static_cast < float > ( r ) * pitch - app.pages_scroll, cell, cell };
				page_entry& entry = app.pages [ page ];
				bool selected = page == app.page_selected;
				bool lit = app.table >= 0 && entry.owner >= 0 && entry.owner < static_cast < int32_t > ( app.owner_count ) && base::same_folded ( app.owners [ entry.owner ], app.db->schema->tables [ app.table ].name );
				ui::color shade = page_color ( entry.kind );

				if ( app.table >= 0 && !lit && entry.kind != storage::PAGE_HEADER && entry.kind != storage::PAGE_FREE )
					shade = ui::mix ( shade, ui::rgb ( THEME_BACKGROUND ), 0.55f );

				ui::fill_round ( draw, box, 2, shade );

				if ( selected )
					ui::outline_round ( draw, { box.x - 2, box.y - 2, box.w + 4, box.h + 4 }, 3, ui::rgb ( THEME_TEXT ), 1.5f );

				if ( ui::inside ( { box.x - 1, box.y - 1, pitch, pitch }, input.x, input.y ) && !ui.active ) {

					base::text line;

					line.add ( "Page " ).number ( page ).add ( "   " ).add ( page_kind_name ( entry.kind ) );

					if ( entry.owner >= 0 && entry.owner < static_cast < int32_t > ( app.owner_count ) )
						line.add ( "   belongs to " ).add ( app.owners [ entry.owner ] );

					hover_text ( ui, line.value ( ) );
					ui.cursor = CURSOR_HAND;

					if ( input.pressed ) {

						ui.active = make_id ( "page.cell", page );
						select_page ( app, page );
						ui.redraw = true;

					}

				}

			}

		}

		ui::pop_clip ( draw );
		app.pages_scroll = scrollbar ( ui, make_id ( "pages.scroll" ), { area.x + area.w - LAYOUT_SCROLLBAR - 4, grid.y, LAYOUT_SCROLLBAR, grid.h }, content, grid.h, app.pages_scroll, true );

		static const uint8_t kinds [ 7 ] = { storage::PAGE_HEADER, storage::PAGE_TABLE_INTERIOR, storage::PAGE_TABLE_LEAF, storage::PAGE_INDEX_INTERIOR, storage::PAGE_INDEX_LEAF, storage::PAGE_OVERFLOW, storage::PAGE_FREE };
		float x = area.x + 20;
		float y = area.y + area.h - 34;

		for ( uint32_t i = 0; i < 7; i++ ) {

			const char* name = page_kind_name ( kinds [ i ] );

			ui::fill_round ( draw, { x, y + 6, 10, 10 }, 2, page_color ( kinds [ i ] ) );
			ui::text ( draw, ui::FONT_SMALL, name, { x + 16, y, 110, 22 }, ui::rgb ( THEME_DIM ) );
			x += 30 + ui::measure ( draw, ui::FONT_SMALL, name );

		}

	}

	static uint32_t byte_shade ( const studio_state& app, uint32_t offset, uint8_t value ) {

		uint8_t kind = app.summary.kind;
		bool tree = kind >= storage::PAGE_TABLE_LEAF && kind <= storage::PAGE_INDEX_INTERIOR;

		if ( offset >= PAGE_USABLE )
			return THEME_NUMBER;

		if ( tree && offset < BTREE_HEADER )
			return THEME_ACCENT_SOFT;

		if ( tree && offset < BTREE_HEADER + app.summary.cells * 2 )
			return THEME_FUNCTION;

		if ( kind == storage::PAGE_OVERFLOW && offset < OVERFLOW_HEADER )
			return THEME_ACCENT_SOFT;

		if ( kind == storage::PAGE_HEADER && offset < 64 )
			return THEME_ACCENT_SOFT;

		return value ? THEME_TEXT : 0x384047;

	}

	static void draw_page_detail ( studio_state& app, const ui::rect& area ) {

		ui_context& ui = app.ui;
		ui::renderer& draw = app.draw;
		input_state& input = app.input;

		ui::fill ( draw, area, ui::rgb ( THEME_PANEL ) );
		ui::line ( draw, area.x + 0.5f, area.y, area.x + 0.5f, area.y + area.h, ui::rgb ( THEME_DIVIDER ) );

		if ( !app.page_loaded ) {

			ui::text ( draw, ui::FONT_UI, "Pick a page on the left", area, ui::rgb ( THEME_FAINT ), ui::ALIGN_CENTER );

			return;

		}

		const storage::page_summary& page = app.summary;
		float x = area.x + 20;
		float w = area.w - 40;
		float y = area.y + 10;
		base::text title;

		title.add ( "Page " ).number ( page.number );
		ui::fill_round ( draw, { x, y + 9, 12, 12 }, 2, page_color ( page.kind ) );
		ui::text ( draw, ui::FONT_TITLE, title.value ( ), title.size, { x + 22, y, w, 30 }, ui::rgb ( THEME_TEXT ) );
		y += 36;

		float column = w * 0.5f;
		base::text value;
		page_entry& entry = app.pages [ page.number ];
		auto pair = [ & ] ( float at_x, float at_y, const char* key ) {

			key_value ( ui, { at_x, at_y, column - 16, 22 }, key, value.value ( ), ui::rgb ( THEME_TEXT ) );
			value.clear ( );

		};

		value.add ( page_kind_name ( page.kind ) );
		pair ( x, y, "Kind" );
		value.add ( entry.owner >= 0 && entry.owner < static_cast < int32_t > ( app.owner_count ) ? app.owners [ entry.owner ] : page.kind == storage::PAGE_HEADER ? "database header" : "none" );
		pair ( x + column, y, "Belongs to" );
		y += 22;
		value.number ( page.cells );
		pair ( x, y, "Cells" );
		value.number ( page.used ).add ( " of " ).number ( PAGE_USABLE );
		pair ( x + column, y, "Bytes used" );
		y += 22;

		if ( page.kind == storage::PAGE_TABLE_LEAF || page.kind == storage::PAGE_INDEX_LEAF ) {

			value.number ( page.previous );
			pair ( x, y, "Previous leaf" );
			value.number ( page.next );
			pair ( x + column, y, "Next leaf" );
			y += 22;

		} else if ( page.child_count ) {

			value.number ( page.child_count );
			pair ( x, y, "Children" );

			for ( uint32_t c = 0; c < page.child_count && c < 6; c++ )
				value.add ( c ? ", " : "" ).number ( page.children [ c ] );

			if ( page.child_count > 6 )
				value.add ( ", \xE2\x80\xA6" );

			pair ( x + column, y, "Pages" );
			y += 22;

		} else if ( page.kind == storage::PAGE_OVERFLOW || page.kind == storage::PAGE_FREE ) {

			value.number ( page.next );
			pair ( x, y, "Next page" );
			y += 22;

		}

		if ( page.has_rowids ) {

			value.signed_number ( page.low_rowid ).add ( " to " ).signed_number ( page.high_rowid );
			pair ( x, y, "Row ids" );
			y += 22;

		}

		float fill = static_cast < float > ( page.used ) / static_cast < float > ( PAGE_USABLE );
		ui::rect bar = { x, y + 8, w, 4 };

		ui::fill_round ( draw, bar, 2, ui::rgb ( THEME_RAISED ) );
		ui::fill_round ( draw, { bar.x, bar.y, bar.w * clamp ( fill, 0, 1 ), bar.h }, 2, page_color ( page.kind ) );
		y += 24;
		ui::line ( draw, area.x, y + 0.5f, area.x + area.w, y + 0.5f, ui::rgb ( THEME_DIVIDER ) );
		caption ( ui, { x, y + 4, w, LAYOUT_CAPTION }, "Bytes", "header   slots   checksum" );
		y += 4 + LAYOUT_CAPTION;

		ui::rect dump = { area.x, y, area.w, area.y + area.h - y };
		float content = 256 * STUDIO_HEX_ROW + 12;

		if ( ui::inside ( dump, input.x, input.y ) && input.wheel != 0 )
			app.hex_scroll -= input.wheel * STUDIO_HEX_ROW * 4;

		app.hex_scroll = clamp ( app.hex_scroll, 0, content - dump.h );
		ui::fill ( draw, dump, ui::rgb ( THEME_BACKGROUND ) );
		ui::push_clip ( draw, dump );

		float advance = draw.code_small_advance;
		uint32_t first = static_cast < uint32_t > ( app.hex_scroll / STUDIO_HEX_ROW );

		for ( uint32_t row = first; row < 256 && static_cast < float > ( row - first ) * STUDIO_HEX_ROW < dump.h + STUDIO_HEX_ROW; row++ ) {

			float line_y = dump.y + 6 + static_cast < float > ( row ) * STUDIO_HEX_ROW - app.hex_scroll;
			base::text offset;

			offset.hex ( row * 16, 4, true );
			ui::text ( draw, ui::FONT_CODE_SMALL, offset.value ( ), offset.size, { x, line_y, 60, STUDIO_HEX_ROW }, ui::rgb ( THEME_FAINT ) );

			float hex_x = x + advance * 6;
			float text_x = hex_x + advance * 50;

			for ( uint32_t b = 0; b < 16; b++ ) {

				uint32_t at = row * 16 + b;
				uint8_t value_byte = app.page_bytes [ at ];
				base::text pair_text;
				char shown = value_byte >= 32 && value_byte < 127 ? static_cast < char > ( value_byte ) : '.';
				float bx = hex_x + static_cast < float > ( b ) * advance * 3 + ( b >= 8 ? advance : 0 );

				pair_text.hex ( value_byte, 2, true );
				ui::text ( draw, ui::FONT_CODE_SMALL, pair_text.value ( ), 2, { bx, line_y, advance * 3, STUDIO_HEX_ROW }, ui::rgb ( byte_shade ( app, at, value_byte ) ) );
				ui::text ( draw, ui::FONT_CODE_SMALL, &shown, 1, { text_x + static_cast < float > ( b ) * advance, line_y, advance * 2, STUDIO_HEX_ROW }, ui::rgb ( value_byte >= 32 && value_byte < 127 ? THEME_DIM : 0x30373D ) );

			}

		}

		ui::pop_clip ( draw );
		app.hex_scroll = scrollbar ( ui, make_id ( "hex.scroll" ), { area.x + area.w - LAYOUT_SCROLLBAR, dump.y, LAYOUT_SCROLLBAR, dump.h }, content, dump.h, app.hex_scroll, true );

	}

	void draw_pages_view ( studio_state& app, const ui::rect& area ) {

		ui::fill ( app.draw, area, ui::rgb ( THEME_BACKGROUND ) );

		if ( !app.db || !app.db->open ) {

			ui::text ( app.draw, ui::FONT_UI, "No database is open", area, ui::rgb ( THEME_FAINT ), ui::ALIGN_CENTER );

			return;

		}

		ensure_pages ( app );

		float detail = clamp ( app.draw.code_small_advance * 72 + 40, 480, area.w * 0.6f );

		draw_page_map ( app, { area.x, area.y, area.w - detail, area.h } );
		draw_page_detail ( app, { area.x + area.w - detail, area.y, detail, area.h } );

	}

}
