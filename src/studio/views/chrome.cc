#include "studio/app/studio.hh"

namespace studio {

	void draw_toolbar ( studio_state& app, const ui::rect& area ) {

		ui_context& ui = app.ui;
		ui::renderer& draw = app.draw;

		ui::fill ( draw, area, ui::rgb ( THEME_PANEL ) );
		ui::line ( draw, area.x, area.y + area.h - 0.5f, area.x + area.w, area.y + area.h - 0.5f, ui::rgb ( THEME_DIVIDER ) );
		ui::text ( draw, ui::FONT_TITLE, "Strata", { area.x + 16, area.y, 80, area.h }, ui::rgb ( THEME_TEXT ) );

		float name_x = area.x + 16 + ui::measure ( draw, ui::FONT_TITLE, "Strata" ) + 12;
		float tabs_x = clamp ( app.sidebar_width, 180, area.w * 0.45f ) + 8;

		if ( app.file_name )
			ui::text ( draw, ui::FONT_SMALL, app.file_name, { name_x, area.y + 1, tabs_x - name_x - 12, area.h }, ui::rgb ( THEME_DIM ) );

		static const char* const names [ 4 ] = { "Data", "Query", "Map", "Pages" };

		for ( uint32_t i = 0; i < 4; i++ ) {

			ui::rect place = { tabs_x + static_cast < float > ( i ) * 78, area.y, 78, area.h };

			if ( tab ( ui, make_id ( "tab", i ), place, names [ i ], app.view == static_cast < view_kind > ( i ) ) )
				app.view = static_cast < view_kind > ( i );

		}

		float right = area.x + area.w - 14;

		if ( app.view == VIEW_QUERY ) {

			ui::rect run = { right - 88, area.y + 8, 88, area.h - 16 };
			ui::rect plan = { right - 88 - 96, area.y + 8, 88, area.h - 16 };

			if ( button ( ui, make_id ( "run" ), run, "Run", true, app.db != nullptr ) )
				run_editor ( app, false );

			if ( button ( ui, make_id ( "explain" ), plan, "Explain", false, app.db != nullptr ) )
				run_editor ( app, true );

			ui::text ( draw, ui::FONT_SMALL, "Ctrl+Enter runs", { plan.x - 160, area.y, 148, area.h }, ui::rgb ( THEME_FAINT ), ui::ALIGN_RIGHT );

		} else if ( app.view == VIEW_MAP ) {

			ui::rect storage = { right - 92, area.y + 9, 92, area.h - 18 };
			ui::rect schema = { right - 184, area.y + 9, 92, area.h - 18 };

			if ( segment ( ui, make_id ( "map.schema" ), schema, "Schema", app.map == MAP_SCHEMA, true, false ) )
				app.map = MAP_SCHEMA;

			if ( segment ( ui, make_id ( "map.storage" ), storage, "Storage", app.map == MAP_STORAGE, false, true ) )
				app.map = MAP_STORAGE;

		} else {

			ui::rect refresh = { right - 88, area.y + 8, 88, area.h - 16 };

			if ( button ( ui, make_id ( "refresh" ), refresh, "Refresh", false, app.db != nullptr ) ) {

				int32_t table = app.table;

				refresh_schema ( app );
				select_table ( app, table );
				set_status ( app, "Reloaded the schema and the rows", false );

			}

		}

	}

	static bool primary_column ( const engine::table_info& table, uint32_t column ) {

		if ( static_cast < int32_t > ( column ) == table.rowid_column )
			return true;

		for ( uint32_t k = 0; k < table.primary_count; k++ )
			if ( table.primary [ k ] == column )
				return true;

		return false;

	}

	static bool foreign_column ( const engine::table_info& table, uint32_t column ) {

		for ( uint32_t f = 0; f < table.foreign_count; f++ )
			for ( uint32_t k = 0; k < table.foreign [ f ].column_count; k++ )
				if ( table.foreign [ f ].columns [ k ] == column )
					return true;

		return false;

	}

	void draw_sidebar ( studio_state& app, const ui::rect& area ) {

		ui_context& ui = app.ui;
		ui::renderer& draw = app.draw;

		ui::fill ( draw, area, ui::rgb ( THEME_PANEL ) );

		if ( !app.db || !app.db->open ) {

			ui::text ( draw, ui::FONT_UI, "Drop a database file here", inset ( area, 16 ), ui::rgb ( THEME_FAINT ), ui::ALIGN_CENTER );

			return;

		}

		engine::catalog& schema = *app.db->schema;

		if ( ui::inside ( area, app.input.x, app.input.y ) && app.input.wheel != 0 )
			app.sidebar_scroll -= app.input.wheel * LAYOUT_ROW * 3;

		ui::push_clip ( draw, area );

		float y = area.y + 8 - app.sidebar_scroll;
		float x = area.x;
		float w = area.w;
		base::text count;

		count.number ( schema.table_count );
		caption ( ui, { x + 16, y, w - 32, LAYOUT_CAPTION }, "Tables", count.value ( ) );
		y += LAYOUT_CAPTION;

		for ( uint32_t t = 0; t < schema.table_count; t++ ) {

			engine::table_info& table = schema.tables [ t ];
			ui::rect row = { x, y, w, 28 };
			bool selected = app.table == static_cast < int32_t > ( t );

			ensure_stats ( app, static_cast < int32_t > ( t ) );

			if ( list_row ( ui, make_id ( "side.table", t ), row, selected ) ) {

				select_table ( app, static_cast < int32_t > ( t ) );

				if ( app.view == VIEW_QUERY )
					app.view = VIEW_DATA;

			}

			table_glyph ( draw, x + 16, y + 8.5f, ui::rgb ( selected ? THEME_ACCENT : THEME_DIM ) );

			base::text rows;

			if ( app.stats [ t ].known )
				rows.number ( app.stats [ t ].rows );

			float rows_w = ui::measure ( draw, ui::FONT_SMALL, rows.value ( ), rows.size );

			ui::text ( draw, selected ? ui::FONT_STRONG : ui::FONT_UI, table.name, { x + 38, y, w - 54 - rows_w, 28 }, ui::rgb ( THEME_TEXT ) );
			ui::text ( draw, ui::FONT_SMALL, rows.value ( ), rows.size, { x, y, w - 14, 28 }, ui::rgb ( THEME_FAINT ), ui::ALIGN_RIGHT );
			y += 28;

			if ( !selected )
				continue;

			for ( uint32_t c = 0; c < table.column_count; c++ ) {

				const engine::column_info& column = table.columns [ c ];
				bool key = primary_column ( table, c );
				bool link = foreign_column ( table, c );

				if ( key )
					ui::circle ( draw, x + 26, y + 11, 2.5f, ui::rgb ( THEME_ACCENT ) );
				else if ( link )
					ui::circle ( draw, x + 26, y + 11, 2.5f, ui::rgb ( THEME_FUNCTION ) );

				float type_w = column.type ? ui::measure ( draw, ui::FONT_SMALL, column.type ) : 0;

				ui::text ( draw, ui::FONT_SMALL, column.name, { x + 38, y, w - 58 - type_w, 22 }, ui::rgb ( key ? THEME_TEXT : 0xB4BCC3 ) );

				if ( column.type )
					ui::text ( draw, ui::FONT_SMALL, column.type, { x, y, w - 14, 22 }, ui::rgb ( THEME_FAINT ), ui::ALIGN_RIGHT );

				y += 22;

			}

			y += 6;

		}

		y += 10;
		count.clear ( );
		count.number ( schema.index_count );
		caption ( ui, { x + 16, y, w - 32, LAYOUT_CAPTION }, "Indexes", count.value ( ) );
		y += LAYOUT_CAPTION;

		for ( uint32_t i = 0; i < schema.index_count; i++ ) {

			engine::index_info& index = schema.indexes [ i ];
			base::text label;

			if ( index.automatic ) {

				engine::table_info* table = engine::find_table ( schema, index.table );

				label.add ( "unique (" );

				for ( uint32_t k = 0; table && k < index.column_count; k++ )
					label.add ( k ? ", " : "" ).add ( table->columns [ index.columns [ k ] ].name );

				label.add ( ')' );

			} else
				label.add ( index.name );

			index_glyph ( draw, x + 16, y + 7.5f, ui::rgb ( THEME_FAINT ) );

			float table_w = ui::measure ( draw, ui::FONT_SMALL, index.table );

			ui::text ( draw, ui::FONT_UI, label.value ( ), label.size, { x + 38, y, w - 58 - table_w, 26 }, ui::rgb ( index.automatic ? THEME_DIM : THEME_TEXT ) );
			ui::text ( draw, ui::FONT_SMALL, index.table, { x, y, w - 14, 26 }, ui::rgb ( THEME_FAINT ), ui::ALIGN_RIGHT );
			y += 26;

		}

		float content = y + app.sidebar_scroll - area.y + 12;

		ui::pop_clip ( draw );
		app.sidebar_scroll = clamp ( app.sidebar_scroll, 0, content - area.h );
		app.sidebar_scroll = scrollbar ( ui, make_id ( "side.scroll" ), { area.x + area.w - LAYOUT_SCROLLBAR, area.y, LAYOUT_SCROLLBAR, area.h }, content, area.h, app.sidebar_scroll, true );

	}

	void draw_status ( studio_state& app, const ui::rect& area ) {

		ui::renderer& draw = app.draw;

		ui::fill ( draw, area, ui::rgb ( THEME_PANEL ) );
		ui::line ( draw, area.x, area.y + 0.5f, area.x + area.w, area.y + 0.5f, ui::rgb ( THEME_DIVIDER ) );

		base::text right;

		if ( app.db && app.db->open ) {

			uint32_t pages = app.db->pages.header.page_count;

			if ( app.db->in_transaction )
				right.add ( "transaction open   " );

			right.number ( pages ).add ( pages == 1 ? " page   " : " pages   " );
			size_text ( right, static_cast < uint64_t > ( pages ) * PAGE_SIZE );
			right.add ( "   write-ahead log   format 1" );

		}

		float right_w = ui::measure ( draw, ui::FONT_SMALL, right.value ( ), right.size );

		ui::text ( draw, ui::FONT_SMALL, right.value ( ), right.size, { area.x, area.y, area.w - 14, area.h }, ui::rgb ( THEME_DIM ), ui::ALIGN_RIGHT );

		if ( app.ui.hover_set )
			ui::text ( draw, ui::FONT_SMALL, app.ui.hover.value ( ), app.ui.hover.size, { area.x + 14, area.y, area.w - right_w - 40, area.h }, ui::rgb ( THEME_TEXT ) );
		else {

			if ( app.status.size )
				ui::circle ( draw, area.x + 18, area.y + area.h * 0.5f, 3, ui::rgb ( app.status_error ? THEME_ERROR : THEME_SUCCESS ) );

			ui::text ( draw, ui::FONT_SMALL, app.status.value ( ), app.status.size, { area.x + 30, area.y, area.w - right_w - 56, area.h }, ui::rgb ( app.status_error ? THEME_ERROR : THEME_DIM ) );

		}

	}

}
