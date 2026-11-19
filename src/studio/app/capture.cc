#include "studio/app/studio.hh"
#include "base/file/file.hh"
#include "base/console/console.hh"

#include <windows.h>

namespace studio {

	static void capture_draw ( void* context ) {

		draw_frame ( *static_cast < studio_state* > ( context ) );

	}

	static bool capture_view ( studio_state& app, const char* folder, const char* name ) {

		base::text path;

		path.add ( folder ).add ( '\\' ).add ( name ).add ( ".png" );

		for ( uint32_t pass = 0; pass < 2; pass++ ) {

			bool fine = ui::capture_frame ( app.draw, STUDIO_CAPTURE_WIDTH, STUDIO_CAPTURE_HEIGHT, path.value ( ), capture_draw, &app );

			finish_input ( app );

			if ( !fine )
				return false;

		}

		return true;

	}

	void script_frame ( studio_state& app ) {

		ui::capture_frame ( app.draw, STUDIO_CAPTURE_WIDTH, STUDIO_CAPTURE_HEIGHT, nullptr, capture_draw, &app );
		finish_input ( app );

	}

	void script_move ( studio_state& app, float x, float y ) {

		app.input.x = x;
		app.input.y = y;
		script_frame ( app );

	}

	void script_press ( studio_state& app, float x, float y ) {

		app.input.x = x;
		app.input.y = y;
		app.input.pressed = true;
		app.input.down = true;
		script_frame ( app );

	}

	void script_release ( studio_state& app ) {

		app.input.released = true;
		app.input.down = false;
		script_frame ( app );

	}

	void script_click ( studio_state& app, float x, float y ) {

		script_press ( app, x, y );
		script_release ( app );

	}

	void script_double_click ( studio_state& app, float x, float y ) {

		script_click ( app, x, y );
		app.input.double_click = true;
		script_click ( app, x, y );

	}

	void script_key ( studio_state& app, uint32_t code, bool ctrl, bool shift ) {

		app.input.ctrl = ctrl;
		app.input.shift = shift;
		app.input.keys [ 0 ] = code;
		app.input.key_count = 1;
		script_frame ( app );
		app.input.ctrl = false;
		app.input.shift = false;

	}

	void script_type ( studio_state& app, const char* text ) {

		for ( const char* at = text; *at; at++ ) {

			app.input.chars [ app.input.char_count++ ] = static_cast < uint8_t > ( *at );

			if ( app.input.char_count == INPUT_CHARS )
				script_frame ( app );

		}

		script_frame ( app );

	}

	void script_wheel ( studio_state& app, float x, float y, float amount ) {

		app.input.x = x;
		app.input.y = y;
		app.input.wheel = amount;
		script_frame ( app );

	}

	bool script_shot ( studio_state& app, const char* folder, const char* name ) {

		return capture_view ( app, folder, name );

	}

	void script_step ( script_check& check, bool passed, const char* what ) {

		if ( !passed && check.fine ) {

			base::write ( what );
			base::write ( " failed in the scripted session\n" );

		}

		check.fine = check.fine && passed;

	}

	void script_saved ( script_check& check, bool saved ) {

		if ( !saved && check.fine )
			base::write ( "a screenshot could not be saved\n" );

		check.fine = check.fine && saved;

	}

	static bool play_basics ( studio_state& app, const char* folder, script_check& check ) {

		auto nav = [ & ] ( uint32_t index ) {

			script_click ( app, 128, LAYOUT_TITLE + 12 + static_cast < float > ( index ) * ( LAYOUT_NAV + 2 ) + LAYOUT_NAV * 0.5f );

		};

		float split = LAYOUT_TITLE + LAYOUT_HEADER_BAR + QUERY_TABS_HEIGHT + app.editor_height;
		float header_y = LAYOUT_TITLE + LAYOUT_HEADER_BAR * 0.5f;
		float right = static_cast < float > ( STUDIO_CAPTURE_WIDTH ) - LAYOUT_MARGIN;

		app.input.x = -100;
		app.input.y = -100;
		nav ( 1 );
		script_step ( check, app.view == VIEW_QUERY, "the Query tab opens" );
		script_click ( app, 700, split - 120 );
		script_key ( app, 'A', true );
		script_type ( app, "SELECT city, count(*) AS people" );
		script_key ( app, VK_RETURN, false );
		script_type ( app, "FROM customers" );
		script_key ( app, VK_RETURN, false );
		script_type ( app, "GROUP BY city ORDER BY people DESC" );
		script_key ( app, VK_ESCAPE, false );
		script_key ( app, VK_RETURN, true );
		script_step ( check, app.result == RESULT_GRID && app.result_grid.rows == 12, "typed SQL runs" );
		script_saved ( check, script_shot ( app, folder, "script-query" ) );
		script_key ( app, 'Z', true );
		script_key ( app, 'Y', true );
		script_key ( app, 'A', true );
		script_type ( app, "SELEC broken FROM nowhere" );
		script_key ( app, VK_F5, false );
		script_step ( check, app.result == RESULT_MESSAGE && app.result_failed, "bad SQL reports an error" );
		script_saved ( check, script_shot ( app, folder, "script-error" ) );
		script_key ( app, 'A', true );
		script_type ( app, "SELECT * FROM orders" );
		script_key ( app, VK_ESCAPE, false );
		script_key ( app, VK_RETURN, true );
		script_step ( check, app.result_grid.rows == 4000, "all orders load" );
		script_click ( app, 640, split + 120 );
		script_key ( app, VK_DOWN, false );
		script_key ( app, VK_DOWN, false );
		script_wheel ( app, 700, 700, -6 );
		script_press ( app, 700, split );
		script_move ( app, 700, split - 70 );
		script_release ( app );
		script_step ( check, app.editor_height < 200, "the editor splitter drags" );
		script_saved ( check, script_shot ( app, folder, "script-grid" ) );
		nav ( 2 );
		script_click ( app, right - 165, header_y );
		script_press ( app, 400, 160 );
		script_move ( app, 520, 520 );
		script_move ( app, 640, 600 );
		script_release ( app );
		script_press ( app, 1000, 700 );
		script_move ( app, 1040, 690 );
		script_release ( app );
		script_step ( check, app.map == MAP_SCHEMA, "the schema map shows" );
		script_saved ( check, script_shot ( app, folder, "script-map" ) );
		script_click ( app, right - 55, header_y );
		script_step ( check, app.map == MAP_STORAGE, "the storage map shows" );
		script_wheel ( app, 800, 500, -2 );
		script_saved ( check, script_shot ( app, folder, "script-storage" ) );
		nav ( 3 );
		script_click ( app, 320, 200 );
		script_wheel ( app, 1200, 600, -3 );
		script_step ( check, app.view == VIEW_PAGES && app.page_loaded, "a page opens in the Pages view" );
		script_saved ( check, script_shot ( app, folder, "script-pages" ) );
		nav ( 0 );
		script_click ( app, 100, LAYOUT_TITLE + 12 + 4 * ( LAYOUT_NAV + 2 ) + 10 + 10 + LAYOUT_CAPTION + 2 + 16 );
		script_press ( app, app.sidebar_width, 600 );
		script_move ( app, app.sidebar_width + 60, 600 );
		script_release ( app );
		script_step ( check, app.sidebar_width > 290, "the sidebar splitter drags" );
		script_key ( app, '2', true );
		script_step ( check, app.view == VIEW_QUERY, "ctrl+2 switches views" );
		script_key ( app, '1', true );
		script_saved ( check, script_shot ( app, folder, "script-data" ) );

		return check.fine;

	}

	int32_t capture_studio ( const char* folder ) {

		studio_state* app = make_state ( );

		if ( !app || !ui::start_renderer ( app->draw ) )
			return 1;

		app->draw.scale = 1.0f;
		app->capturing = true;

		char path [ FILE_PATH_ROOM ];

		if ( !base::temp_path ( path, sizeof ( path ), "strata-capture.sdb" ) )
			return 1;

		base::erase_path ( path );

		base::text log;

		log.add ( path ).add ( "-wal" );
		base::erase_path ( log.value ( ) );

		if ( !prepare_sample ( *app, path ) )
			return 1;

		app->input.x = -100;
		app->input.y = -100;

		bool fine = true;

		app->view = VIEW_DATA;
		select_table ( *app, 0 );
		fine = fine && capture_view ( *app, folder, "data" );
		app->view = VIEW_QUERY;
		run_editor ( *app, false );
		fine = fine && capture_view ( *app, folder, "query" );
		run_editor ( *app, true );
		fine = fine && capture_view ( *app, folder, "plan" );
		app->view = VIEW_MAP;
		app->map = MAP_SCHEMA;
		fine = fine && capture_view ( *app, folder, "map" );

		for ( uint32_t i = 0; i < app->db->schema->table_count; i++ )
			if ( base::same ( app->db->schema->tables [ i ].name, "orders" ) )
				select_table ( *app, static_cast < int32_t > ( i ) );

		app->map = MAP_STORAGE;
		fine = fine && capture_view ( *app, folder, "storage" );
		app->view = VIEW_PAGES;
		ensure_pages ( *app );
		select_page ( *app, app->db->schema->tables [ app->table ].root );
		fine = fine && capture_view ( *app, folder, "pages" );

		script_check check = { true, folder };

		if ( fine && ( !play_basics ( *app, folder, check ) || !play_features ( *app, folder, check ) ) ) {

			base::write ( "the scripted session failed\n" );
			fine = false;

		}

		engine::close_database ( *app->db );
		ui::stop_renderer ( app->draw );
		base::erase_path ( path );
		base::erase_path ( log.value ( ) );

		return fine ? 0 : 1;

	}

}