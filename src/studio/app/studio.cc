#include "studio/app/studio.hh"
#include "sql/print/print.hh"
#include "base/file/file.hh"

#include <windows.h>

namespace studio {

	void size_text ( base::text& out, uint64_t bytes ) {

		if ( bytes < 1024 )
			out.number ( bytes ).add ( " B" );
		else if ( bytes < 1024 * 1024 )
			out.decimal ( bytes * 10 / 1024, 10, 1 ).add ( " KB" );
		else if ( bytes < 1024ull * 1024 * 1024 )
			out.decimal ( bytes * 10 / ( 1024 * 1024 ), 10, 1 ).add ( " MB" );
		else
			out.decimal ( bytes * 100 / ( 1024ull * 1024 * 1024 ), 100, 2 ).add ( " GB" );

	}

	void grouped ( base::text& out, uint64_t value ) {

		char digits [ 32 ];
		uint32_t count = 0;

		do {

			if ( count && count % 4 == 3 )
				digits [ count++ ] = ',';

			digits [ count++ ] = static_cast < char > ( '0' + value % 10 );
			value /= 10;

		} while ( value );

		while ( count )
			out.add ( digits [ --count ] );

	}

	ui::color page_color ( uint8_t kind ) {

		switch ( kind ) {

			case storage::PAGE_HEADER: return ui::rgb ( THEME_PAGE_HEADER );
			case storage::PAGE_TABLE_INTERIOR: return ui::rgb ( THEME_PAGE_TABLE_INTERIOR );
			case storage::PAGE_TABLE_LEAF: return ui::rgb ( THEME_PAGE_TABLE_LEAF );
			case storage::PAGE_INDEX_INTERIOR: return ui::rgb ( THEME_PAGE_INDEX_INTERIOR );
			case storage::PAGE_INDEX_LEAF: return ui::rgb ( THEME_PAGE_INDEX_LEAF );
			case storage::PAGE_OVERFLOW: return ui::rgb ( THEME_PAGE_OVERFLOW );
			default: return ui::rgb ( THEME_PAGE_FREE );

		}

	}

	const char* page_kind_name ( uint8_t kind ) {

		switch ( kind ) {

			case storage::PAGE_HEADER: return "header";
			case storage::PAGE_FREE: return "free";
			case storage::PAGE_TABLE_INTERIOR: return "table interior";
			case storage::PAGE_TABLE_LEAF: return "table leaf";
			case storage::PAGE_INDEX_INTERIOR: return "index interior";
			case storage::PAGE_INDEX_LEAF: return "index leaf";
			case storage::PAGE_OVERFLOW: return "overflow";
			default: return "unknown";

		}

	}

	void set_status ( studio_state& app, const char* message, bool error ) {

		app.status.clear ( );
		app.status.add ( message );
		app.status_error = error;

	}

	static double now_ms ( studio_state& app ) {

		LARGE_INTEGER counter;

		QueryPerformanceCounter ( &counter );

		return static_cast < double > ( counter.QuadPart ) * 1000.0 / static_cast < double > ( app.frequency ? app.frequency : 1 );

	}

	static bool count_row ( void* context, const storage::value* values, uint32_t count ) {

		if ( count && values [ 0 ].kind == storage::VALUE_INTEGER )
			*static_cast < uint64_t* > ( context ) = static_cast < uint64_t > ( values [ 0 ].integer );

		return true;

	}

	void ensure_stats ( studio_state& app, int32_t table ) {

		if ( table < 0 || !app.db || table >= static_cast < int32_t > ( app.db->schema->table_count ) || app.stats [ table ].known )
			return;

		engine::table_info& info = app.db->schema->tables [ table ];
		table_stats& stats = app.stats [ table ];
		base::text sql;
		engine::query_sink sink = { &stats.rows, nullptr, count_row };

		sql.add ( "SELECT count(*) FROM " );
		sql::print_name ( sql, info.name );
		stats.rows = 0;
		engine::execute ( *app.db, sql.value ( ), nullptr, 0, &sink );

		storage::tree_report report;

		storage::check_btree ( app.db->pages, info.root, false, report );
		stats.pages = report.pages;
		stats.leaves = report.leaves;
		stats.depth = report.depth;
		stats.overflow = report.overflow;
		stats.used = report.used_bytes;
		stats.known = true;

	}

	void refresh_schema ( studio_state& app ) {

		for ( uint32_t i = 0; i < CATALOG_TABLES; i++ )
			app.stats [ i ].known = false;

		app.cards_ready = false;
		app.trees_for = -1;
		app.pages_ready = false;
		app.page_loaded = false;
		app.edit.active = false;

		uint32_t count = app.db ? app.db->schema->table_count : 0;

		if ( app.table >= static_cast < int32_t > ( count ) )
			app.table = count ? 0 : -1;

		if ( app.table < 0 && count )
			app.table = 0;

	}

	static void load_rows ( studio_state& app ) {

		uint64_t id = app.data_grid.id;

		clear_grid ( app.data_grid );
		app.data_grid.id = id;
		app.data_grid.hidden = 1;

		if ( !app.db || app.table < 0 || app.table >= static_cast < int32_t > ( app.db->schema->table_count ) )
			return;

		engine::table_info& info = app.db->schema->tables [ app.table ];
		base::text sql;
		engine::query_sink sink = { &app.data_grid, grid_columns, grid_row };

		sql.add ( "SELECT rowid, * FROM " );
		sql::print_name ( sql, info.name );
		sql.add ( " LIMIT " ).number ( STUDIO_DATA_LIMIT );

		if ( !engine::execute ( *app.db, sql.value ( ), nullptr, 0, &sink ) )
			set_status ( app, app.db->problem.value ( ), true );

		app.data_grid.hidden = 1;
		finish_grid ( app.data_grid, app.draw );

	}

	void select_table ( studio_state& app, int32_t table ) {

		app.table = table;
		app.trees_for = -1;
		app.edit.active = false;
		load_rows ( app );
		ensure_stats ( app, table );

	}

	void reload_table ( studio_state& app ) {

		float scroll_x = app.data_grid.scroll_x;
		float scroll_y = app.data_grid.scroll_y;
		int32_t row = app.data_grid.selected_row;
		int32_t column = app.data_grid.selected_column;
		grid_column widths [ 64 ];
		uint32_t kept = app.data_grid.column_count < 64 ? app.data_grid.column_count : 64;

		for ( uint32_t c = 0; c < kept; c++ )
			widths [ c ] = app.data_grid.columns [ c ];

		if ( app.table >= 0 && app.table < static_cast < int32_t > ( CATALOG_TABLES ) )
			app.stats [ app.table ].known = false;

		load_rows ( app );
		ensure_stats ( app, app.table );

		if ( app.data_grid.column_count == kept )
			for ( uint32_t c = 0; c < kept; c++ )
				app.data_grid.columns [ c ].width = widths [ c ].width;

		app.data_grid.scroll_x = scroll_x;
		app.data_grid.scroll_y = scroll_y;
		app.data_grid.selected_row = row < static_cast < int32_t > ( app.data_grid.rows ) ? row : static_cast < int32_t > ( app.data_grid.rows ) - 1;
		app.data_grid.selected_column = column;

	}

	bool open_database_file ( studio_state& app, const char* path ) {

		if ( !app.db ) {

			app.db = base::make < engine::database > ( 1 );

			if ( !app.db )
				return false;

		}

		engine::database* fresh = base::make < engine::database > ( 1 );

		if ( !fresh )
			return false;

		if ( !engine::open_database ( *fresh, path ) ) {

			base::text message;

			message.add ( "Could not open " ).add ( path ).add ( ": " ).add ( fresh->problem.value ( ) );
			set_status ( app, message.value ( ), true );
			fresh->problem.~text ( );
			base::release ( fresh );

			return false;

		}

		engine::close_database ( *app.db );
		app.db->problem.~text ( );
		base::release ( app.db );
		app.db = fresh;

		base::copy_text ( app.path, sizeof ( app.path ), path );
		app.file_name = app.path;

		for ( const char* at = app.path; *at; at++ )
			if ( *at == '\\' || *at == '/' )
				app.file_name = at + 1;

		app.table = -1;
		engine::forget_report ( app.check );
		refresh_schema ( app );
		select_table ( app, app.table );
		remember_file ( app, path );

		base::text message;

		message.add ( "Opened " ).add ( app.file_name );
		set_status ( app, message.value ( ), false );

		if ( app.window ) {

			base::text title;
			wchar_t wide [ FILE_PATH_ROOM ];

			title.add ( app.file_name ).add ( " \xE2\x80\x94 Strata Studio" );

			if ( MultiByteToWideChar ( CP_UTF8, 0, title.value ( ), -1, wide, FILE_PATH_ROOM ) > 0 )
				SetWindowTextW ( app.window, wide );

		}

		return true;

	}

	static bool plan_row_sink ( void* context, const storage::value* values, uint32_t count ) {

		plan_data& plan = *static_cast < plan_data* > ( context );

		if ( count < 3 )
			return true;

		plan_row row = { };

		row.id = values [ 0 ].integer;
		row.parent = values [ 1 ].integer;
		row.detail = plan.memory.keep ( reinterpret_cast < const char* > ( values [ 2 ].bytes ), values [ 2 ].size );

		for ( uint32_t i = 0; i < plan.count; i++ )
			if ( plan.rows [ i ].id == row.parent )
				row.depth = plan.rows [ i ].depth + 1;

		return base::push ( plan.memory, plan.rows, plan.count, plan.room, row );

	}

	static bool plan_columns ( void* context, const char* const* names, uint32_t count ) {

		plan_data& plan = *static_cast < plan_data* > ( context );

		( void ) names;
		( void ) count;
		plan.memory.reset ( );
		plan.rows = nullptr;
		plan.count = 0;
		plan.room = 0;

		return true;

	}

	bool run_sql ( studio_state& app, const char* sql, const storage::value* parameters, uint32_t count ) {

		if ( !app.db )
			return false;

		if ( engine::execute ( *app.db, sql, parameters, count, nullptr ) )
			return true;

		set_status ( app, app.db->problem.value ( ), true );

		return false;

	}

	void after_change ( studio_state& app ) {

		int32_t table = app.table;
		uint32_t version = app.db ? app.db->schema->version : 0;

		refresh_schema ( app );

		if ( app.db && table >= 0 && table < static_cast < int32_t > ( app.db->schema->table_count ) )
			app.table = table;

		( void ) version;
		reload_table ( app );
		engine::forget_report ( app.check );

	}

	void run_editor ( studio_state& app, bool explain ) {

		if ( !app.db )
			return;

		base::text body;
		base::text sql;
		size_t base_offset = 0;

		editor_run_text ( app.editor, body, base_offset );

		if ( !body.size )
			return;

		note_history ( app, body.value ( ), body.size );

		if ( explain )
			sql.add ( "EXPLAIN QUERY PLAN " );

		sql.add ( body.value ( ), body.size );

		uint32_t version = app.db->schema->version;
		int64_t changes = app.db->total_changes;
		bool was_open = app.db->in_transaction;
		double started = now_ms ( app );
		bool fine = false;

		if ( explain ) {

			engine::query_sink sink = { &app.plan, plan_columns, plan_row_sink };

			fine = engine::execute ( *app.db, sql.value ( ), nullptr, 0, &sink );

		} else {

			uint64_t id = app.result_grid.id;

			clear_grid ( app.result_grid );
			app.result_grid.id = id;
			app.plan.memory.reset ( );
			app.plan.rows = nullptr;
			app.plan.count = 0;
			app.plan.room = 0;

			engine::query_sink sink = { &app.result_grid, grid_columns, grid_row };

			fine = engine::execute ( *app.db, sql.value ( ), nullptr, 0, &sink );
			finish_grid ( app.result_grid, app.draw );
			app.result_sql.clear ( );

			if ( fine && app.result_grid.loaded )
				app.result_sql.add ( body.value ( ), body.size );

			reset_chart ( app.chart, app.result_grid );

		}

		app.result_ms = now_ms ( app ) - started;
		app.result_failed = !fine;
		app.result_message.clear ( );

		if ( !fine ) {

			app.result = RESULT_MESSAGE;
			app.result_message.add ( app.db->problem.value ( ) );

			if ( app.db->parse_failed && !explain ) {

				uint32_t line = app.db->error_line;
				uint32_t column = app.db->error_column;
				uint32_t base_line = 0;
				uint32_t base_column = 0;

				for ( size_t i = 0; i < base_offset; i++ ) {

					if ( app.editor.text.data [ i ] == '\n' ) {

						base_line++;
						base_column = 0;

					} else
						base_column++;

				}

				editor_place ( app.editor, line + base_line, line == 1 ? column + base_column : column );
				app.result_message.add ( " (line " ).number ( line + base_line ).add ( ", column " ).number ( line == 1 ? column + base_column : column ).add ( ')' );

			}

			set_status ( app, "The statement failed", true );

		} else {

			base::text message;

			if ( explain ) {

				app.result = RESULT_PLAN;
				message.add ( "Plan with " ).number ( app.plan.count ).add ( app.plan.count == 1 ? " step" : " steps" );

			} else if ( app.result_grid.loaded ) {

				app.result = app.result == RESULT_CHART ? RESULT_CHART : RESULT_GRID;
				grouped ( message, app.result_grid.rows );
				message.add ( app.result_grid.rows == 1 ? " row" : " rows" );

				if ( app.result_grid.truncated )
					message.add ( " shown, more were cut off" );

			} else {

				app.result = RESULT_MESSAGE;
				message.add ( "Done" );

				if ( app.db->changes )
					message.add ( ", " ).signed_number ( app.db->changes ).add ( app.db->changes == 1 ? " row changed" : " rows changed" );

			}

			message.add ( " in " ).decimal ( static_cast < uint64_t > ( app.result_ms * 100 ), 100, 2 ).add ( " ms" );
			app.result_message.add ( message.value ( ) );
			set_status ( app, message.value ( ), false );

		}

		if ( version != app.db->schema->version || changes != app.db->total_changes || app.db->in_transaction || was_open )
			after_change ( app );

	}

	void ensure_trees ( studio_state& app ) {

		if ( app.trees_for == app.table || !app.db )
			return;

		app.trees_for = app.table;
		app.tree_count = 0;
		app.tree_memory.reset ( );

		if ( app.table < 0 )
			return;

		engine::catalog& schema = *app.db->schema;
		engine::table_info& table = schema.tables [ app.table ];
		storage::page_summary* summary = base::make < storage::page_summary > ( 1 );

		if ( !summary )
			return;

		auto build = [ & ] ( const char* name, uint32_t root, bool index ) {

			if ( app.tree_count == STUDIO_TREES )
				return;

			static const uint32_t limit = 12000;
			tree_view& tree = app.trees [ app.tree_count++ ];

			tree = { };
			tree.name = name;
			tree.index = index;
			tree.root = root;
			tree.nodes = app.tree_memory.make < tree_node > ( limit );

			if ( !tree.nodes )
				return;

			tree.nodes [ 0 ] = { };
			tree.nodes [ 0 ].page = root;
			tree.nodes [ 0 ].parent = -1;
			tree.count = 1;

			for ( uint32_t at = 0; at < tree.count; at++ ) {

				tree_node& node = tree.nodes [ at ];

				if ( !storage::inspect_page ( app.db->pages, node.page, *summary ) )
					continue;

				node.kind = summary->kind;
				node.cells = summary->cells;
				node.used = summary->used;
				node.has_rowids = summary->has_rowids;
				node.low = summary->low_rowid;
				node.high = summary->high_rowid;
				node.first_child = tree.count;
				node.child_count = 0;

				if ( node.level + 1 > tree.levels )
					tree.levels = node.level + 1;

				if ( !summary->child_count )
					tree.leaves++;

				for ( uint32_t c = 0; c < summary->child_count; c++ ) {

					if ( tree.count == limit ) {

						tree.clipped = true;

						break;

					}

					tree_node& child = tree.nodes [ tree.count++ ];

					child = { };
					child.page = summary->children [ c ];
					child.parent = static_cast < int32_t > ( at );
					child.level = node.level + 1;
					tree.nodes [ at ].child_count++;

				}

			}

		};

		build ( table.name, table.root, false );

		for ( uint32_t i = 0; i < schema.index_count; i++ )
			if ( base::same_folded ( schema.indexes [ i ].table, table.name ) )
				build ( schema.indexes [ i ].name, schema.indexes [ i ].root, true );

		base::release ( summary );

	}

	void ensure_pages ( studio_state& app ) {

		if ( app.pages_ready || !app.db )
			return;

		base::release ( app.pages );
		app.page_total = app.db->pages.header.page_count;
		app.pages = base::make < page_entry > ( app.page_total + 1 );
		app.owner_count = 0;
		app.pages_ready = true;

		if ( !app.pages )
			return;

		storage::page_summary* summary = base::make < storage::page_summary > ( 1 );

		if ( !summary )
			return;

		for ( uint32_t p = 1; p <= app.page_total; p++ ) {

			app.pages [ p ].owner = -1;

			if ( storage::inspect_page ( app.db->pages, p, *summary ) )
				app.pages [ p ].kind = summary->kind;

		}

		engine::catalog& schema = *app.db->schema;
		uint32_t* queue = base::make < uint32_t > ( app.page_total + 1 );

		auto claim = [ & ] ( const char* name, uint32_t root ) {

			int32_t owner = static_cast < int32_t > ( app.owner_count );
			uint32_t count = 0;

			app.owners [ app.owner_count++ ] = name;

			if ( !queue || !root || root > app.page_total )
				return;

			queue [ count++ ] = root;

			for ( uint32_t at = 0; at < count; at++ ) {

				uint32_t page = queue [ at ];

				if ( app.pages [ page ].owner >= 0 )
					continue;

				app.pages [ page ].owner = owner;

				if ( !storage::inspect_page ( app.db->pages, page, *summary ) )
					continue;

				for ( uint32_t c = 0; c < summary->child_count && count <= app.page_total; c++ )
					if ( summary->children [ c ] && summary->children [ c ] <= app.page_total )
						queue [ count++ ] = summary->children [ c ];

			}

		};

		claim ( "strata schema", app.db->pages.header.schema_root );

		for ( uint32_t i = 0; i < schema.table_count; i++ )
			claim ( schema.tables [ i ].name, schema.tables [ i ].root );

		for ( uint32_t i = 0; i < schema.index_count; i++ )
			claim ( schema.indexes [ i ].name, schema.indexes [ i ].root );

		base::release ( queue );
		base::release ( summary );

		if ( !app.page_selected || app.page_selected > app.page_total )
			select_page ( app, 1 );

	}

	void select_page ( studio_state& app, uint32_t page ) {

		if ( !app.db || !page || page > app.db->pages.header.page_count )
			return;

		app.page_selected = page;
		app.page_loaded = storage::inspect_page ( app.db->pages, page, app.summary ) && storage::read_page_bytes ( app.db->pages, page, app.page_bytes );
		app.hex_scroll = 0;

	}

	void run_check ( studio_state& app ) {

		if ( !app.db )
			return;

		double started = now_ms ( app );
		bool clean = engine::check_database ( *app.db, app.check );
		base::text message;

		if ( clean ) {

			message.add ( "No problems in " );
			grouped ( message, app.check.pages );
			message.add ( " pages, " ).number ( app.check.trees ).add ( " trees and " );
			grouped ( message, app.check.rows );
			message.add ( " rows" );

		} else {

			message.number ( app.check.problem_count ).add ( app.check.problem_count == 1 ? " problem found" : " problems found" );

		}

		message.add ( " in " ).decimal ( static_cast < uint64_t > ( ( now_ms ( app ) - started ) * 10 ), 10, 1 ).add ( " ms" );
		set_status ( app, message.value ( ), !clean );
		app.check_scroll = 0;
		app.overlay = OVERLAY_CHECK;

	}

	static void handle_shortcuts ( studio_state& app ) {

		ui_context& ui = app.ui;
		input_state& input = app.input;

		if ( input.ctrl ) {

			for ( uint32_t k = 0; k < input.key_count; k++ ) {

				uint32_t key = input.keys [ k ];

				if ( key >= '1' && key <= '4' ) {

					app.view = static_cast < view_kind > ( key - '1' );
					ui.redraw = true;

				}

			}

			if ( key_hit ( ui, 'O' ) )
				open_picker ( app );

			if ( app.view == VIEW_QUERY && key_hit ( ui, 'T' ) )
				add_tab ( app, "" );

			if ( app.view == VIEW_QUERY && key_hit ( ui, 'W' ) && app.tab_count > 1 )
				close_tab ( app, app.tab );

		}

		if ( app.view == VIEW_QUERY && ui.focus != app.editor.id && key_hit ( ui, VK_F5 ) )
			run_editor ( app, false );

	}

	void draw_frame ( studio_state& app ) {

		ui::renderer& draw = app.draw;
		ui_context& ui = app.ui;
		float width = static_cast < float > ( draw.width ) / draw.scale;
		float height = static_cast < float > ( draw.height ) / draw.scale;

		ui.draw = &draw;
		ui.input = &app.input;
		ui.hot = 0;
		ui.cursor = CURSOR_ARROW;
		ui.hover_set = false;

		bool modal = app.overlay != OVERLAY_NONE;
		input_state held = app.input;

		if ( modal && key_hit ( ui, VK_ESCAPE ) ) {

			app.overlay = OVERLAY_NONE;
			modal = false;
			app.input.key_count = 0;

		}

		if ( modal ) {

			app.input.pressed = false;
			app.input.released = false;
			app.input.double_click = false;
			app.input.right_pressed = false;
			app.input.wheel = 0;
			app.input.key_count = 0;
			app.input.char_count = 0;
			app.input.x = -100000;
			app.input.y = -100000;

		} else
			handle_shortcuts ( app );

		ui::fill ( draw, { 0, 0, width, height }, ui::rgb ( THEME_BACKGROUND ) );

		float sidebar = clamp ( app.sidebar_width, 200, width * 0.4f );
		ui::rect title = { 0, 0, width, LAYOUT_TITLE };
		ui::rect status = { 0, height - LAYOUT_STATUS, width, LAYOUT_STATUS };
		ui::rect side = { 0, LAYOUT_TITLE, sidebar, height - LAYOUT_TITLE - LAYOUT_STATUS };
		ui::rect content = { sidebar + 1, LAYOUT_TITLE, width - sidebar - 1, side.h };

		switch ( app.view ) {

			case VIEW_DATA: draw_data_view ( app, content ); break;
			case VIEW_QUERY: draw_query_view ( app, content ); break;
			case VIEW_MAP: draw_map_view ( app, content ); break;
			case VIEW_PAGES: draw_pages_view ( app, content ); break;

		}

		draw_sidebar ( app, side );
		ui::line ( draw, sidebar + 0.5f, LAYOUT_TITLE, sidebar + 0.5f, height - LAYOUT_STATUS, ui::rgb ( THEME_DIVIDER ) );
		splitter ( ui, make_id ( "sidebar.split" ), { sidebar - 3, LAYOUT_TITLE, 6, side.h }, true, app.sidebar_width, 200, width * 0.4f );
		draw_titlebar ( app, title );
		draw_status ( app, status );

		if ( !modal )
			return;

		app.input = held;
		ui.hot = 0;

		ui::rect whole = { 0, 0, width, height };

		switch ( app.overlay ) {

			case OVERLAY_OPEN: draw_open_overlay ( app, whole ); break;
			case OVERLAY_TRANSFER: draw_transfer_overlay ( app, whole ); break;
			case OVERLAY_DESIGN: draw_designer_overlay ( app, whole ); break;
			case OVERLAY_CHECK: draw_check_overlay ( app, whole ); break;
			default: break;

		}

	}

}