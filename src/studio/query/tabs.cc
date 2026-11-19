#include "studio/app/studio.hh"

namespace studio {

	static void swap_tab ( studio_state& app, query_tab& slot ) {

		base::swap_bytes ( &app.editor, &slot.editor, sizeof ( editor_state ) );
		base::swap_bytes ( &app.result_grid, &slot.grid, sizeof ( grid_data ) );
		base::swap_bytes ( &app.plan, &slot.plan, sizeof ( plan_data ) );
		base::swap_bytes ( &app.result_message, &slot.message, sizeof ( base::text ) );
		base::swap_bytes ( &app.result_sql, &slot.sql, sizeof ( base::text ) );
		base::swap_bytes ( &app.result, &slot.result, sizeof ( result_mode ) );
		base::swap_bytes ( &app.result_failed, &slot.failed, sizeof ( bool ) );
		base::swap_bytes ( &app.result_ms, &slot.ms, sizeof ( double ) );

	}

	static void prepare_slot ( studio_state& app, query_tab& slot, const char* sql ) {

		slot.editor.id = make_id ( "editor" );
		slot.editor.marked = -1;
		slot.editor.suggest = suggest_schema;
		slot.editor.suggest_context = &app;
		set_editor_text ( slot.editor, sql );
		clear_grid ( slot.grid );
		slot.grid.id = make_id ( "grid.result" );
		slot.result = RESULT_NONE;
		slot.failed = false;
		slot.ms = 0;
		slot.number = ++app.tab_numbers;

	}

	static void forget_slot ( query_tab& slot ) {

		slot.editor.text.~text ( );

		for ( uint32_t i = 0; i < EDITOR_UNDO; i++ )
			slot.editor.undo [ i ].~text ( );

		base::release ( slot.editor.lines );
		base::release ( slot.editor.shades );
		slot.grid.memory.reset ( );
		slot.plan.memory.reset ( );
		slot.message.~text ( );
		slot.sql.~text ( );
		base::fill ( &slot, 0, sizeof ( slot ) );

	}

	void start_tabs ( studio_state& app ) {

		app.tab_count = 1;
		app.tab = 0;
		app.tab_numbers = 1;
		app.tabs [ 0 ].number = 1;
		app.editor.suggest = suggest_schema;
		app.editor.suggest_context = &app;

	}

	void switch_tab ( studio_state& app, uint32_t tab ) {

		if ( tab >= app.tab_count || tab == app.tab )
			return;

		uint32_t number = app.tabs [ app.tab ].number;

		swap_tab ( app, app.tabs [ app.tab ] );
		app.tabs [ app.tab ].number = number;
		swap_tab ( app, app.tabs [ tab ] );
		app.tab = tab;
		reset_chart ( app.chart, app.result_grid );
		app.ui.focus = app.editor.id;
		app.ui.redraw = true;

	}

	void add_tab ( studio_state& app, const char* sql ) {

		if ( app.tab_count == STUDIO_TABS ) {

			set_status ( app, "Close a query tab before opening another", true );

			return;

		}

		uint32_t made = app.tab_count++;

		prepare_slot ( app, app.tabs [ made ], sql );
		switch_tab ( app, made );

	}

	void close_tab ( studio_state& app, uint32_t tab ) {

		if ( tab >= app.tab_count )
			return;

		if ( app.tab_count == 1 ) {

			set_editor_text ( app.editor, "" );

			return;

		}

		if ( tab == app.tab )
			switch_tab ( app, tab ? tab - 1 : tab + 1 );

		forget_slot ( app.tabs [ tab ] );

		for ( uint32_t i = tab; i + 1 < app.tab_count; i++ )
			base::copy ( &app.tabs [ i ], &app.tabs [ i + 1 ], sizeof ( query_tab ) );

		app.tab_count--;
		base::fill ( &app.tabs [ app.tab_count ], 0, sizeof ( query_tab ) );

		if ( app.tab > tab )
			app.tab--;

	}

	static void tab_title ( const editor_state& editor, uint32_t number, base::text& out ) {

		const char* text = editor.text.value ( );
		size_t at = 0;

		out.clear ( );

		while ( at < editor.text.size && ( text [ at ] == ' ' || text [ at ] == '\n' || text [ at ] == '\t' || text [ at ] == '\r' ) )
			at++;

		size_t end = at;

		while ( end < editor.text.size && text [ end ] != '\n' && end - at < 28 )
			end++;

		if ( end > at ) {

			out.add ( text + at, end - at );

			if ( end < editor.text.size && text [ end ] != '\n' )
				out.add ( "\xE2\x80\xA6" );

		} else
			out.add ( "Query " ).number ( number );

	}

	void draw_query_tabs ( studio_state& app, const ui::rect& area ) {

		ui_context& ui = app.ui;
		ui::renderer& draw = app.draw;
		float x = area.x + 12;
		int32_t closing = -1;
		int32_t chosen = -1;

		ui::fill ( draw, area, ui::rgb ( THEME_PANEL ) );
		ui::line ( draw, area.x, area.y + area.h - 0.5f, area.x + area.w, area.y + area.h - 0.5f, ui::rgb ( THEME_DIVIDER ) );

		for ( uint32_t i = 0; i < app.tab_count; i++ ) {

			base::text title;
			const editor_state& editor = i == app.tab ? app.editor : app.tabs [ i ].editor;
			bool active = i == app.tab;

			tab_title ( editor, app.tabs [ i ].number ? app.tabs [ i ].number : i + 1, title );

			float w = clamp ( ui::measure ( draw, ui::FONT_UI, title.value ( ), title.size ) + 58, 110, 240 );
			ui::rect place = { x, area.y + 5, w, area.h - 5 };
			uint64_t id = make_id ( "query.tab", i );
			bool hover = ui::inside ( place, app.input.x, app.input.y ) && ( !ui.active || ui.active == id );

			if ( active ) {

				ui::fill_round ( draw, { place.x, place.y, place.w, place.h + 4 }, LAYOUT_RADIUS, ui::rgb ( THEME_BACKGROUND ) );
				ui::outline_round ( draw, { place.x, place.y, place.w, place.h + 6 }, LAYOUT_RADIUS, ui::rgb ( THEME_DIVIDER ) );
				ui::fill ( draw, { place.x + 1, place.y + place.h - 1, place.w - 2, 3 }, ui::rgb ( THEME_BACKGROUND ) );
				ui::fill_round ( draw, { place.x + 8, place.y, place.w - 16, 2 }, 1, ui::rgb ( THEME_ACCENT ) );

			} else if ( hover )
				ui::fill_round ( draw, place, LAYOUT_RADIUS, ui::rgb ( THEME_HOVER ) );

			draw_icon ( draw, ICON_QUERY, place.x + 12, place.y + ( place.h - ICON_SIZE ) * 0.5f, ui::rgb ( active ? THEME_ACCENT : THEME_FAINT ) );
			ui::text ( draw, ui::FONT_UI, title.value ( ), title.size, { place.x + 36, place.y, place.w - 64, place.h }, ui::rgb ( active ? THEME_TEXT : THEME_DIM ) );

			ui::rect close = { place.x + place.w - 28, place.y + ( place.h - 22 ) * 0.5f, 22, 22 };

			if ( ( active || hover ) && app.tab_count > 1 && icon_only ( ui, make_id ( "query.tab.close", i ), close, ICON_CLOSE, "Close this tab (Ctrl+W)" ) )
				closing = static_cast < int32_t > ( i );
			else if ( hover ) {

				ui.hot = id;

				if ( app.input.pressed && !ui::inside ( close, app.input.x, app.input.y ) )
					chosen = static_cast < int32_t > ( i );

			}

			x += w + 4;

		}

		if ( icon_only ( ui, make_id ( "query.tab.add" ), { x + 2, area.y + 6, 28, area.h - 10 }, ICON_PLUS, "New query tab (Ctrl+T)", app.tab_count < STUDIO_TABS ) )
			add_tab ( app, "" );

		if ( closing >= 0 )
			close_tab ( app, static_cast < uint32_t > ( closing ) );
		else if ( chosen >= 0 )
			switch_tab ( app, static_cast < uint32_t > ( chosen ) );

	}

}