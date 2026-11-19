#include "studio/app/studio.hh"

#include <windows.h>

namespace studio {

	static bool window_button_hit ( studio_state& app, uint64_t id, const ui::rect& area, icon_kind icon, bool close ) {

		ui_context& ui = app.ui;
		ui::renderer& draw = app.draw;
		bool hover = ui::inside ( area, app.input.x, app.input.y ) && ( !ui.active || ui.active == id );

		if ( hover ) {

			ui.hot = id;

			if ( app.input.pressed )
				ui.active = id;

		}

		bool held = ui.active == id && app.input.down;

		if ( hover || held )
			ui::fill ( draw, area, close ? ui::rgb ( THEME_CLOSE, held ? 0.8f : 1.0f ) : ui::rgb ( held ? THEME_PRESSED : THEME_HOVER ) );

		draw_icon ( draw, icon, area.x + ( area.w - ICON_SIZE ) * 0.5f, area.y + ( area.h - ICON_SIZE ) * 0.5f, ui::rgb ( hover && close ? 0xFFFFFF : THEME_DIM ) );

		return ui.active == id && app.input.released && hover;

	}

	void draw_titlebar ( studio_state& app, const ui::rect& area ) {

		ui_context& ui = app.ui;
		ui::renderer& draw = app.draw;

		ui::fill ( draw, area, ui::rgb ( THEME_CHROME ) );
		ui::line ( draw, area.x, area.y + area.h - 0.5f, area.x + area.w, area.y + area.h - 0.5f, ui::rgb ( THEME_DIVIDER ) );
		draw_icon ( draw, ICON_LOGO, area.x + 18, area.y + ( area.h - ICON_SIZE ) * 0.5f, ui::rgb ( THEME_ACCENT ) );
		ui::text ( draw, ui::FONT_BRAND, "Strata", { area.x + 44, area.y, 80, area.h }, ui::rgb ( THEME_TEXT ) );

		float x = area.x + 44 + ui::measure ( draw, ui::FONT_BRAND, "Strata" ) + 14;

		if ( app.file_name ) {

			ui::text ( draw, ui::FONT_UI, "/", { x, area.y, 12, area.h }, ui::rgb ( THEME_FAINT ) );
			ui::text ( draw, ui::FONT_UI, app.file_name, { x + 16, area.y, area.w * 0.4f, area.h }, ui::rgb ( THEME_DIM ) );

		}

		float right = area.x + area.w;
		ui::rect close = { right - LAYOUT_WINDOW_BUTTON, area.y, LAYOUT_WINDOW_BUTTON, area.h - 1 };
		ui::rect maximize = { close.x - LAYOUT_WINDOW_BUTTON, area.y, LAYOUT_WINDOW_BUTTON, area.h - 1 };
		ui::rect minimize = { maximize.x - LAYOUT_WINDOW_BUTTON, area.y, LAYOUT_WINDOW_BUTTON, area.h - 1 };
		float open_w = icon_button_width ( draw, "Open" );
		ui::rect open = { minimize.x - 14 - open_w, area.y + 6, open_w, area.h - 12 };

		if ( icon_button ( ui, make_id ( "title.open" ), open, ICON_OPEN, "Open" ) )
			open_picker ( app );

		if ( window_button_hit ( app, make_id ( "title.minimize" ), minimize, ICON_MINIMIZE, false ) && app.window )
			ShowWindow ( app.window, SW_MINIMIZE );

		if ( window_button_hit ( app, make_id ( "title.maximize" ), maximize, app.maximized ? ICON_RESTORE : ICON_MAXIMIZE, false ) && app.window )
			ShowWindow ( app.window, app.maximized ? SW_RESTORE : SW_MAXIMIZE );

		if ( window_button_hit ( app, make_id ( "title.close" ), close, ICON_CLOSE, true ) && app.window )
			PostMessageW ( app.window, WM_CLOSE, 0, 0 );

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

	static void draw_navigation ( studio_state& app, float x, float y, float w ) {

		ui_context& ui = app.ui;
		ui::renderer& draw = app.draw;
		static const char* const names [ 4 ] = { "Data", "Query", "Map", "Pages" };
		static const char* const keys [ 4 ] = { "Ctrl+1", "Ctrl+2", "Ctrl+3", "Ctrl+4" };
		static const icon_kind icons [ 4 ] = { ICON_DATA, ICON_QUERY, ICON_MAP, ICON_PAGES };

		for ( uint32_t i = 0; i < 4; i++ ) {

			ui::rect row = { x + 12, y + static_cast < float > ( i ) * ( LAYOUT_NAV + 2 ), w - 24, LAYOUT_NAV };
			uint64_t id = make_id ( "nav", i );
			bool selected = app.view == static_cast < view_kind > ( i );
			bool hover = ui::inside ( row, app.input.x, app.input.y ) && ( !ui.active || ui.active == id );

			if ( hover ) {

				ui.hot = id;

				if ( app.input.pressed )
					ui.active = id;

				if ( ui.active == id && app.input.released ) {

					app.view = static_cast < view_kind > ( i );
					ui.redraw = true;

				}

			}

			if ( selected ) {

				ui::fill_round ( draw, row, LAYOUT_RADIUS, ui::rgb ( THEME_RAISED ) );
				ui::fill_round ( draw, { row.x, row.y + 8, 3, row.h - 16 }, 1.5f, ui::rgb ( THEME_ACCENT ) );

			} else if ( hover )
				ui::fill_round ( draw, row, LAYOUT_RADIUS, ui::rgb ( THEME_HOVER ) );

			draw_icon ( draw, icons [ i ], row.x + 14, row.y + ( row.h - ICON_SIZE ) * 0.5f, ui::rgb ( selected ? THEME_ACCENT : THEME_DIM ) );
			ui::text ( draw, selected ? ui::FONT_STRONG : ui::FONT_UI, names [ i ], { row.x + 42, row.y, row.w - 110, row.h }, ui::rgb ( selected ? THEME_TEXT : 0xB8C0C7 ) );

			if ( hover )
				ui::text ( draw, ui::FONT_SMALL, keys [ i ], { row.x, row.y, row.w - 12, row.h }, ui::rgb ( THEME_FAINT ), ui::ALIGN_RIGHT );

		}

	}

	void draw_sidebar ( studio_state& app, const ui::rect& area ) {

		ui_context& ui = app.ui;
		ui::renderer& draw = app.draw;
		float x = area.x;
		float w = area.w;
		float nav_h = 12 + 4 * ( LAYOUT_NAV + 2 ) + 10;

		ui::fill ( draw, area, ui::rgb ( THEME_PANEL ) );
		draw_navigation ( app, x, area.y + 12, w );
		ui::line ( draw, x + 20, area.y + nav_h - 0.5f, x + w - 20, area.y + nav_h - 0.5f, ui::rgb ( THEME_DIVIDER ) );

		ui::rect list = { area.x, area.y + nav_h, area.w, area.h - nav_h };

		if ( !app.db || !app.db->open ) {

			ui::text ( draw, ui::FONT_UI, "No database open", inset ( list, 20 ), ui::rgb ( THEME_FAINT ), ui::ALIGN_CENTER );

			return;

		}

		engine::catalog& schema = *app.db->schema;

		if ( ui::inside ( list, app.input.x, app.input.y ) && app.input.wheel != 0 )
			app.sidebar_scroll -= app.input.wheel * LAYOUT_ROW * 3;

		ui::push_clip ( draw, list );

		float y = list.y + 10 - app.sidebar_scroll;
		base::text count;

		count.number ( schema.table_count );
		caption ( ui, { x + 24, y, w - 82, LAYOUT_CAPTION }, "Tables", count.value ( ) );

		if ( icon_only ( ui, make_id ( "side.new" ), { x + w - 52, y + 3, 28, 28 }, ICON_PLUS, "Make a new table" ) )
			open_designer ( app, -1 );

		y += LAYOUT_CAPTION + 2;

		for ( uint32_t t = 0; t < schema.table_count; t++ ) {

			engine::table_info& table = schema.tables [ t ];
			ui::rect row = { x + 12, y, w - 24, 32 };
			bool selected = app.table == static_cast < int32_t > ( t );

			ensure_stats ( app, static_cast < int32_t > ( t ) );

			bool hover = ui::inside ( row, app.input.x, app.input.y ) && ui::inside ( list, app.input.x, app.input.y ) && !ui.active;

			if ( selected )
				ui::fill_round ( draw, row, LAYOUT_RADIUS, ui::rgb ( THEME_SELECTED ) );
			else if ( hover )
				ui::fill_round ( draw, row, LAYOUT_RADIUS, ui::rgb ( THEME_HOVER ) );

			if ( hover && app.input.pressed ) {

				ui.active = make_id ( "side.table", t );
				select_table ( app, static_cast < int32_t > ( t ) );

				if ( app.view != VIEW_MAP && app.view != VIEW_PAGES )
					app.view = VIEW_DATA;

				ui.redraw = true;

			}

			draw_icon ( draw, ICON_TABLE, row.x + 12, row.y + 8, ui::rgb ( selected ? THEME_ACCENT : THEME_FAINT ) );

			base::text rows;

			if ( app.stats [ t ].known )
				grouped ( rows, app.stats [ t ].rows );

			float rows_w = ui::measure ( draw, ui::FONT_SMALL, rows.value ( ), rows.size );

			ui::text ( draw, selected ? ui::FONT_STRONG : ui::FONT_UI, table.name, { row.x + 38, row.y, row.w - 50 - rows_w, row.h }, ui::rgb ( THEME_TEXT ) );
			ui::text ( draw, ui::FONT_SMALL, rows.value ( ), rows.size, { row.x, row.y, row.w - 12, row.h }, ui::rgb ( THEME_FAINT ), ui::ALIGN_RIGHT );
			y += 34;

			if ( !selected )
				continue;

			for ( uint32_t c = 0; c < table.column_count; c++ ) {

				const engine::column_info& column = table.columns [ c ];
				bool key = primary_column ( table, c );
				bool link = foreign_column ( table, c );

				if ( key )
					draw_icon ( draw, ICON_KEY, x + 38, y + 4, ui::rgb ( THEME_ACCENT ) );
				else if ( link )
					ui::circle ( draw, x + 46, y + 12, 2.5f, ui::rgb ( THEME_FUNCTION ) );
				else
					ui::circle ( draw, x + 46, y + 12, 1.5f, ui::rgb ( THEME_FAINT ) );

				float type_w = column.type ? ui::measure ( draw, ui::FONT_SMALL, column.type ) : 0;

				ui::text ( draw, ui::FONT_SMALL, column.name, { x + 62, y, w - 90 - type_w, 24 }, ui::rgb ( key ? THEME_TEXT : 0xB4BCC3 ) );

				if ( column.type )
					ui::text ( draw, ui::FONT_SMALL, column.type, { x, y, w - 24, 24 }, ui::rgb ( THEME_FAINT ), ui::ALIGN_RIGHT );

				y += 24;

			}

			y += 8;

		}

		y += 14;
		count.clear ( );
		count.number ( schema.index_count );
		caption ( ui, { x + 24, y, w - 48, LAYOUT_CAPTION }, "Indexes", count.value ( ) );
		y += LAYOUT_CAPTION + 2;

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

			draw_icon ( draw, ICON_INDEX, x + 24, y + 7, ui::rgb ( index.fulltext ? THEME_FUNCTION : THEME_FAINT ) );

			float table_w = ui::measure ( draw, ui::FONT_SMALL, index.table );

			ui::text ( draw, ui::FONT_UI, label.value ( ), label.size, { x + 50, y, w - 82 - table_w, 30 }, ui::rgb ( index.automatic ? THEME_DIM : THEME_TEXT ) );
			ui::text ( draw, ui::FONT_SMALL, index.table, { x, y, w - 24, 30 }, ui::rgb ( THEME_FAINT ), ui::ALIGN_RIGHT );
			y += 30;

		}

		float content = y + app.sidebar_scroll - list.y + 16;

		ui::pop_clip ( draw );
		app.sidebar_scroll = clamp ( app.sidebar_scroll, 0, content - list.h );
		app.sidebar_scroll = scrollbar ( ui, make_id ( "side.scroll" ), { area.x + area.w - LAYOUT_SCROLLBAR, list.y, LAYOUT_SCROLLBAR, list.h }, content, list.h, app.sidebar_scroll, true );

	}

	void draw_status ( studio_state& app, const ui::rect& area ) {

		ui::renderer& draw = app.draw;

		ui::fill ( draw, area, ui::rgb ( THEME_CHROME ) );
		ui::line ( draw, area.x, area.y + 0.5f, area.x + area.w, area.y + 0.5f, ui::rgb ( THEME_DIVIDER ) );

		float right = area.x + area.w - 18;
		auto item = [ & ] ( const char* value, size_t size, uint32_t shade ) {

			float w = ui::measure ( draw, ui::FONT_SMALL, value, size );

			ui::text ( draw, ui::FONT_SMALL, value, size, { right - w, area.y, w + 2, area.h }, ui::rgb ( shade ) );
			right -= w + 22;

		};

		item ( STUDIO_VERSION, base::length ( STUDIO_VERSION ), THEME_FAINT );

		if ( app.db && app.db->open ) {

			uint32_t pages = app.db->pages.header.page_count;
			base::text size;
			base::text count;

			size_text ( size, static_cast < uint64_t > ( pages ) * PAGE_SIZE );
			grouped ( count, pages );
			count.add ( pages == 1 ? " page" : " pages" );
			item ( size.value ( ), size.size, THEME_DIM );
			item ( count.value ( ), count.size, THEME_DIM );

			if ( app.db->in_transaction )
				item ( "Transaction open", 16, THEME_WARNING );

		}

		float left_w = right - area.x - 40;

		if ( app.ui.hover_set )
			ui::text ( draw, ui::FONT_SMALL, app.ui.hover.value ( ), app.ui.hover.size, { area.x + 18, area.y, left_w, area.h }, ui::rgb ( THEME_TEXT ) );
		else {

			if ( app.status.size )
				ui::circle ( draw, area.x + 20, area.y + area.h * 0.5f, 3.5f, ui::rgb ( app.status_error ? THEME_ERROR : THEME_SUCCESS ) );

			ui::text ( draw, ui::FONT_SMALL, app.status.value ( ), app.status.size, { area.x + 34, area.y, left_w, area.h }, ui::rgb ( app.status_error ? THEME_ERROR : THEME_DIM ) );

		}

	}

	int32_t draw_page_header ( studio_state& app, const ui::rect& area, const char* title, const char* subtitle, const header_action* actions, uint32_t count ) {

		ui_context& ui = app.ui;
		ui::renderer& draw = app.draw;
		ui::rect bar = { area.x, area.y, area.w, LAYOUT_HEADER_BAR };
		int32_t clicked = -1;

		ui::fill ( draw, bar, ui::rgb ( THEME_BACKGROUND ) );
		ui::line ( draw, bar.x, bar.y + bar.h - 0.5f, bar.x + bar.w, bar.y + bar.h - 0.5f, ui::rgb ( THEME_DIVIDER ) );

		float x = bar.x + bar.w - LAYOUT_MARGIN;

		for ( uint32_t i = count; i-- > 0; ) {

			const header_action& action = actions [ i ];
			float w = icon_button_width ( draw, action.label );
			ui::rect place = { x - w, bar.y + ( bar.h - LAYOUT_BUTTON ) * 0.5f, w, LAYOUT_BUTTON };

			if ( icon_button ( ui, make_id ( action.label, i + 1 ) ^ make_id ( title ), place, action.icon, action.label, action.primary, action.enabled ) )
				clicked = static_cast < int32_t > ( i );

			x -= w + 10;

		}

		app.header_left = x;

		float title_w = x - bar.x - LAYOUT_MARGIN - 20;

		if ( subtitle && *subtitle ) {

			ui::text ( draw, ui::FONT_TITLE, title, { bar.x + LAYOUT_MARGIN, bar.y + 12, title_w, 30 }, ui::rgb ( THEME_TEXT ) );
			ui::text ( draw, ui::FONT_SMALL, subtitle, { bar.x + LAYOUT_MARGIN, bar.y + 42, title_w, 20 }, ui::rgb ( THEME_DIM ) );

		} else
			ui::text ( draw, ui::FONT_TITLE, title, { bar.x + LAYOUT_MARGIN, bar.y, title_w, bar.h }, ui::rgb ( THEME_TEXT ) );

		return clicked;

	}

	float draw_stat ( studio_state& app, float x, float y, float w, const char* label, const char* value, const char* detail, bool divider ) {

		ui_context& ui = app.ui;
		ui::renderer& draw = app.draw;

		caption ( ui, { x, y + 14, w - 24, 18 }, label );
		ui::text ( draw, ui::FONT_VALUE, value, { x, y + 32, w - 24, 30 }, ui::rgb ( THEME_TEXT ) );

		if ( detail && *detail )
			ui::text ( draw, ui::FONT_SMALL, detail, { x, y + 61, w - 24, 18 }, ui::rgb ( THEME_DIM ) );

		if ( divider )
			ui::line ( draw, x + w - 24.5f, y + 16, x + w - 24.5f, y + LAYOUT_STATS - 14, ui::rgb ( THEME_DIVIDER ) );

		return x + w;

	}

	ui::rect draw_overlay_frame ( studio_state& app, const ui::rect& area, float width, float height, const char* title ) {

		ui_context& ui = app.ui;
		ui::renderer& draw = app.draw;
		float w = clamp ( width, 320, area.w - 60 );
		float h = clamp ( height, 200, area.h - 60 );
		ui::rect panel = { area.x + ( area.w - w ) * 0.5f, area.y + ( area.h - h ) * 0.5f, w, h };

		ui::fill ( draw, area, ui::rgb ( 0x000000, 0.55f ) );
		ui::fill_round ( draw, { panel.x + 3, panel.y + 6, panel.w, panel.h }, LAYOUT_DIALOG_RADIUS, ui::rgb ( 0x000000, 0.35f ) );
		ui::fill_round ( draw, panel, LAYOUT_DIALOG_RADIUS, ui::rgb ( THEME_PANEL ) );
		ui::outline_round ( draw, panel, LAYOUT_DIALOG_RADIUS, ui::rgb ( THEME_BORDER ) );
		ui::text ( draw, ui::FONT_TITLE, title, { panel.x + LAYOUT_MARGIN, panel.y + 8, panel.w - 120, 56 }, ui::rgb ( THEME_TEXT ) );
		ui::line ( draw, panel.x, panel.y + 64.5f, panel.x + panel.w, panel.y + 64.5f, ui::rgb ( THEME_DIVIDER ) );

		if ( icon_only ( ui, make_id ( "overlay.close" ), { panel.x + panel.w - 50, panel.y + 16, 32, 32 }, ICON_CLOSE, "Close" ) )
			app.overlay = OVERLAY_NONE;

		if ( app.input.pressed && !ui::inside ( panel, app.input.x, app.input.y ) )
			app.overlay = OVERLAY_NONE;

		return { panel.x + LAYOUT_MARGIN, panel.y + 80, panel.w - LAYOUT_MARGIN * 2, panel.h - 96 };

	}

}