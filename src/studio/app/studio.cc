#include "studio/app/studio.hh"
#include "sql/print/print.hh"
#include "base/file/file.hh"

#include <windows.h>
#include <shellapi.h>
#include <dwmapi.h>

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

		uint32_t count = app.db ? app.db->schema->table_count : 0;

		if ( app.table >= static_cast < int32_t > ( count ) )
			app.table = count ? 0 : -1;

		if ( app.table < 0 && count )
			app.table = 0;

	}

	void select_table ( studio_state& app, int32_t table ) {

		app.table = table;
		app.trees_for = -1;
		clear_grid ( app.data_grid );

		if ( !app.db || table < 0 || table >= static_cast < int32_t > ( app.db->schema->table_count ) )
			return;

		engine::table_info& info = app.db->schema->tables [ table ];
		base::text sql;
		engine::query_sink sink = { &app.data_grid, grid_columns, grid_row };

		sql.add ( "SELECT * FROM " );
		sql::print_name ( sql, info.name );
		sql.add ( " LIMIT 20000" );

		if ( !engine::execute ( *app.db, sql.value ( ), nullptr, 0, &sink ) )
			set_status ( app, app.db->problem.value ( ), true );

		finish_grid ( app.data_grid, app.draw );
		ensure_stats ( app, table );

	}

	bool open_database_file ( studio_state& app, const char* path ) {

		if ( !app.db ) {

			app.db = base::make < engine::database > ( 1 );

			if ( !app.db )
				return false;

		}

		engine::close_database ( *app.db );

		if ( !engine::open_database ( *app.db, path ) ) {

			base::text message;

			message.add ( "Could not open " ).add ( path ).add ( ": " ).add ( app.db->problem.value ( ) );
			set_status ( app, message.value ( ), true );

			return false;

		}

		base::copy_text ( app.path, sizeof ( app.path ), path );
		app.file_name = app.path;

		for ( const char* at = app.path; *at; at++ )
			if ( *at == '\\' || *at == '/' )
				app.file_name = at + 1;

		app.table = -1;
		refresh_schema ( app );
		select_table ( app, app.table );

		base::text message;

		message.add ( "Opened " ).add ( app.file_name );
		set_status ( app, message.value ( ), false );

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

		plan.memory.reset ( );
		plan.rows = nullptr;
		plan.count = 0;
		plan.room = 0;

		return true;

	}

	void run_editor ( studio_state& app, bool explain ) {

		if ( !app.db )
			return;

		base::text body;
		base::text sql;
		size_t base_offset = 0;

		editor_run_text ( app.editor, body, base_offset );

		if ( explain )
			sql.add ( "EXPLAIN QUERY PLAN " );

		sql.add ( body.value ( ), body.size );

		uint32_t version = app.db->schema->version;
		double started = now_ms ( app );
		bool fine = false;

		if ( explain ) {

			engine::query_sink sink = { &app.plan, plan_columns, plan_row_sink };

			fine = engine::execute ( *app.db, sql.value ( ), nullptr, 0, &sink );

		} else {

			uint64_t id = app.result_grid.id;

			clear_grid ( app.result_grid );
			app.result_grid.id = id;

			engine::query_sink sink = { &app.result_grid, grid_columns, grid_row };

			fine = engine::execute ( *app.db, sql.value ( ), nullptr, 0, &sink );
			finish_grid ( app.result_grid, app.draw );

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

				app.result = RESULT_GRID;
				message.number ( app.result_grid.rows ).add ( app.result_grid.rows == 1 ? " row" : " rows" );

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

		if ( version != app.db->schema->version || !explain ) {

			int32_t table = app.table;

			refresh_schema ( app );

			if ( table >= 0 && table < static_cast < int32_t > ( app.db->schema->table_count ) )
				app.table = table;

			select_table ( app, app.table );

		}

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

		if ( app.input.ctrl ) {

			for ( uint32_t k = 0; k < app.input.key_count; k++ ) {

				uint32_t key = app.input.keys [ k ];

				if ( key >= '1' && key <= '4' ) {

					app.view = static_cast < view_kind > ( key - '1' );
					ui.redraw = true;

				}

			}

		}

		if ( app.view == VIEW_QUERY && ui.focus != app.editor.id && key_hit ( ui, VK_F5 ) )
			run_editor ( app, false );

		ui::fill ( draw, { 0, 0, width, height }, ui::rgb ( THEME_BACKGROUND ) );

		float sidebar = clamp ( app.sidebar_width, 180, width * 0.45f );
		ui::rect toolbar = { 0, 0, width, LAYOUT_TOOLBAR };
		ui::rect status = { 0, height - LAYOUT_STATUS, width, LAYOUT_STATUS };
		ui::rect side = { 0, LAYOUT_TOOLBAR, sidebar, height - LAYOUT_TOOLBAR - LAYOUT_STATUS };
		ui::rect content = { sidebar + 1, LAYOUT_TOOLBAR, width - sidebar - 1, side.h };

		switch ( app.view ) {

			case VIEW_DATA: draw_data_view ( app, content ); break;
			case VIEW_QUERY: draw_query_view ( app, content ); break;
			case VIEW_MAP: draw_map_view ( app, content ); break;
			case VIEW_PAGES: draw_pages_view ( app, content ); break;

		}

		draw_sidebar ( app, side );
		ui::line ( draw, sidebar + 0.5f, LAYOUT_TOOLBAR, sidebar + 0.5f, height - LAYOUT_STATUS, ui::rgb ( THEME_DIVIDER ) );
		splitter ( ui, make_id ( "sidebar.split" ), { sidebar - 3, LAYOUT_TOOLBAR, 6, side.h }, true, app.sidebar_width, 180, width * 0.45f );
		draw_toolbar ( app, toolbar );
		draw_status ( app, status );

	}

	static void request_frame ( studio_state& app ) {

		InvalidateRect ( app.window, nullptr, FALSE );

	}

	static void render ( studio_state& app ) {

		if ( !ui::begin_frame ( app.draw ) )
			return;

		draw_frame ( app );
		ui::end_frame ( app.draw );

		input_state& input = app.input;

		if ( !input.down )
			app.ui.active = 0;

		input.pressed = false;
		input.released = false;
		input.double_click = false;
		input.right_pressed = false;
		input.wheel = 0;
		input.key_count = 0;
		input.char_count = 0;

		if ( app.ui.redraw ) {

			app.ui.redraw = false;
			request_frame ( app );

		}

	}

	static void modifiers ( input_state& input ) {

		input.shift = GetKeyState ( VK_SHIFT ) < 0;
		input.ctrl = GetKeyState ( VK_CONTROL ) < 0;
		input.alt = GetKeyState ( VK_MENU ) < 0;

	}

	static LRESULT CALLBACK window_proc ( HWND window, UINT message, WPARAM wide, LPARAM lparam ) {

		studio_state* app = reinterpret_cast < studio_state* > ( GetWindowLongPtrW ( window, GWLP_USERDATA ) );

		if ( !app || !app->draw.factory || !app->draw.target )
			return DefWindowProcW ( window, message, wide, lparam );

		input_state& input = app->input;
		float scale = app->draw.scale > 0 ? app->draw.scale : 1.0f;

		switch ( message ) {

			case WM_MOUSEMOVE:
				input.x = static_cast < float > ( static_cast < int16_t > ( LOWORD ( lparam ) ) ) / scale;
				input.y = static_cast < float > ( static_cast < int16_t > ( HIWORD ( lparam ) ) ) / scale;
				modifiers ( input );
				request_frame ( *app );

				return 0;

			case WM_LBUTTONDOWN:
			case WM_LBUTTONDBLCLK:
				input.x = static_cast < float > ( static_cast < int16_t > ( LOWORD ( lparam ) ) ) / scale;
				input.y = static_cast < float > ( static_cast < int16_t > ( HIWORD ( lparam ) ) ) / scale;
				input.pressed = true;
				input.down = true;
				input.double_click = message == WM_LBUTTONDBLCLK;
				modifiers ( input );
				SetCapture ( window );
				render ( *app );

				return 0;

			case WM_LBUTTONUP:
				input.released = true;
				input.down = false;
				modifiers ( input );
				ReleaseCapture ( );
				render ( *app );

				return 0;

			case WM_RBUTTONDOWN:
				input.right_pressed = true;
				request_frame ( *app );

				return 0;

			case WM_MOUSEWHEEL: {

				POINT point = { static_cast < int16_t > ( LOWORD ( lparam ) ), static_cast < int16_t > ( HIWORD ( lparam ) ) };

				ScreenToClient ( window, &point );
				input.x = static_cast < float > ( point.x ) / scale;
				input.y = static_cast < float > ( point.y ) / scale;
				input.wheel += static_cast < float > ( static_cast < int16_t > ( HIWORD ( wide ) ) ) / WHEEL_DELTA;
				modifiers ( input );
				render ( *app );

				return 0;

			}

			case WM_KEYDOWN:
			case WM_SYSKEYDOWN:
				modifiers ( input );

				if ( input.key_count < INPUT_KEYS )
					input.keys [ input.key_count++ ] = static_cast < uint32_t > ( wide );

				render ( *app );

				if ( message == WM_SYSKEYDOWN && wide != VK_F10 )
					return DefWindowProcW ( window, message, wide, lparam );

				return 0;

			case WM_CHAR: {

				uint32_t code = static_cast < uint32_t > ( wide );

				if ( code >= 0xD800 && code < 0xDC00 ) {

					app->surrogate = code;

					return 0;

				}

				if ( code >= 0xDC00 && code < 0xE000 && app->surrogate )
					code = 0x10000 + ( ( app->surrogate - 0xD800 ) << 10 ) + ( code - 0xDC00 );

				app->surrogate = 0;
				modifiers ( input );

				if ( input.char_count < INPUT_CHARS )
					input.chars [ input.char_count++ ] = code;

				render ( *app );

				return 0;

			}

			case WM_SIZE:
				ui::resize_window ( app->draw, LOWORD ( lparam ), HIWORD ( lparam ), scale );
				request_frame ( *app );

				return 0;

			case WM_DPICHANGED: {

				RECT* suggested = reinterpret_cast < RECT* > ( lparam );

				app->draw.scale = static_cast < float > ( HIWORD ( wide ) ) / 96.0f;
				ui::attach_window ( app->draw, window, app->draw.scale );
				SetWindowPos ( window, nullptr, suggested->left, suggested->top, suggested->right - suggested->left, suggested->bottom - suggested->top, SWP_NOZORDER | SWP_NOACTIVATE );

				return 0;

			}

			case WM_SETCURSOR:
				if ( LOWORD ( lparam ) == HTCLIENT ) {

					const wchar_t* shape = IDC_ARROW;

					switch ( app->ui.cursor ) {

						case CURSOR_TEXT: shape = IDC_IBEAM; break;
						case CURSOR_SIZE_WE: shape = IDC_SIZEWE; break;
						case CURSOR_SIZE_NS: shape = IDC_SIZENS; break;
						case CURSOR_HAND: shape = IDC_HAND; break;
						case CURSOR_MOVE: shape = IDC_SIZEALL; break;
						default: break;

					}

					SetCursor ( LoadCursorW ( nullptr, shape ) );

					return TRUE;

				}

				break;

			case WM_PAINT: {

				PAINTSTRUCT paint;

				BeginPaint ( window, &paint );
				render ( *app );
				EndPaint ( window, &paint );

				return 0;

			}

			case WM_ERASEBKGND:
				return 1;

			case WM_DROPFILES: {

				HDROP drop = reinterpret_cast < HDROP > ( wide );
				wchar_t name [ MAX_PATH ];

				if ( DragQueryFileW ( drop, 0, name, MAX_PATH ) ) {

					char path [ FILE_PATH_ROOM ];

					if ( WideCharToMultiByte ( CP_UTF8, 0, name, -1, path, sizeof ( path ), nullptr, nullptr ) > 0 )
						open_database_file ( *app, path );

				}

				DragFinish ( drop );
				request_frame ( *app );

				return 0;

			}

			case WM_DESTROY:
				PostQuitMessage ( 0 );

				return 0;

			default:
				break;

		}

		return DefWindowProcW ( window, message, wide, lparam );

	}

	static studio_state* make_state ( ) {

		studio_state* app = base::make < studio_state > ( 1 );

		if ( !app )
			return nullptr;

		LARGE_INTEGER frequency;

		QueryPerformanceFrequency ( &frequency );
		app->frequency = static_cast < uint64_t > ( frequency.QuadPart );
		app->sidebar_width = LAYOUT_SIDEBAR;
		app->editor_height = 250;
		app->table = -1;
		app->trees_for = -1;
		app->view = VIEW_DATA;
		app->dragging_card = -1;
		app->data_grid.id = make_id ( "grid.data" );
		app->result_grid.id = make_id ( "grid.result" );
		app->editor.id = make_id ( "editor" );
		app->editor.marked = -1;
		clear_grid ( app->data_grid );
		clear_grid ( app->result_grid );
		app->data_grid.id = make_id ( "grid.data" );
		app->result_grid.id = make_id ( "grid.result" );
		set_editor_text ( app->editor, "SELECT c.name, count(o.id) AS orders, round(sum(i.quantity * p.price), 2) AS spent\nFROM customers c\nJOIN orders o ON o.customer = c.id\nJOIN order_items i ON i.order_id = o.id\nJOIN products p ON p.id = i.product\nGROUP BY c.id\nORDER BY spent DESC\nLIMIT 50;" );

		return app;

	}

	static bool sample_path ( char* out, size_t room ) {

		wchar_t wide [ MAX_PATH ];
		DWORD size = GetEnvironmentVariableW ( L"USERPROFILE", wide, MAX_PATH );

		if ( !size || size >= MAX_PATH )
			return false;

		char folder [ FILE_PATH_ROOM ];

		if ( WideCharToMultiByte ( CP_UTF8, 0, wide, -1, folder, sizeof ( folder ), nullptr, nullptr ) <= 0 )
			return false;

		base::text path;

		path.add ( folder ).add ( "\\Documents\\Strata" );

		wchar_t directory [ FILE_PATH_ROOM ];

		if ( !base::wide_path ( path.value ( ), directory, FILE_PATH_ROOM ) )
			return false;

		CreateDirectoryW ( directory, nullptr );
		path.add ( "\\sample.sdb" );
		base::copy_text ( out, room, path.value ( ) );

		return true;

	}

	static bool prepare_sample ( studio_state& app, const char* path ) {

		bool fresh = !base::path_exists ( path );

		if ( !open_database_file ( app, path ) )
			return false;

		if ( fresh || !app.db->schema->table_count ) {

			if ( !build_sample ( *app.db ) ) {

				set_status ( app, app.db->problem.value ( ), true );

				return false;

			}

			refresh_schema ( app );
			select_table ( app, 0 );
			set_status ( app, "Made a sample database to explore", false );

		}

		return true;

	}

	int32_t run_studio ( const char* path ) {

		SetProcessDpiAwarenessContext ( DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2 );

		studio_state* app = make_state ( );

		if ( !app || !ui::start_renderer ( app->draw ) )
			return 1;

		HINSTANCE instance = GetModuleHandleW ( nullptr );
		WNDCLASSEXW kind = { };

		kind.cbSize = sizeof ( kind );
		kind.style = CS_DBLCLKS;
		kind.lpfnWndProc = window_proc;
		kind.hInstance = instance;
		kind.hCursor = LoadCursorW ( nullptr, IDC_ARROW );
		kind.hIcon = LoadIconW ( instance, MAKEINTRESOURCEW ( 1 ) );
		kind.lpszClassName = L"strata_studio";
		RegisterClassExW ( &kind );

		POINT origin = { 0, 0 };
		HMONITOR monitor = MonitorFromPoint ( origin, MONITOR_DEFAULTTOPRIMARY );
		MONITORINFO area = { sizeof ( area ) };
		UINT dpi = GetDpiForSystem ( );
		float scale = static_cast < float > ( dpi ) / 96.0f;

		GetMonitorInfoW ( monitor, &area );

		int32_t width = static_cast < int32_t > ( 1440 * scale );
		int32_t height = static_cast < int32_t > ( 900 * scale );
		int32_t work_w = area.rcWork.right - area.rcWork.left;
		int32_t work_h = area.rcWork.bottom - area.rcWork.top;

		if ( width > work_w - 40 )
			width = work_w - 40;

		if ( height > work_h - 40 )
			height = work_h - 40;

		HWND window = CreateWindowExW ( WS_EX_ACCEPTFILES, L"strata_studio", L"Strata Studio", WS_OVERLAPPEDWINDOW, area.rcWork.left + ( work_w - width ) / 2, area.rcWork.top + ( work_h - height ) / 2, width, height, nullptr, nullptr, instance, nullptr );

		if ( !window )
			return 1;

		BOOL dark = TRUE;
		COLORREF caption = RGB ( 0x0E, 0x0E, 0x11 );
		COLORREF text = RGB ( 0xE3, 0xE3, 0xE8 );

		DwmSetWindowAttribute ( window, 20, &dark, sizeof ( dark ) );
		DwmSetWindowAttribute ( window, 35, &caption, sizeof ( caption ) );
		DwmSetWindowAttribute ( window, 36, &text, sizeof ( text ) );

		app->window = window;
		ui::attach_window ( app->draw, window, static_cast < float > ( GetDpiForWindow ( window ) ) / 96.0f );
		SetWindowLongPtrW ( window, GWLP_USERDATA, reinterpret_cast < LONG_PTR > ( app ) );

		if ( path && *path )
			open_database_file ( *app, path );
		else {

			char sample [ FILE_PATH_ROOM ];

			if ( sample_path ( sample, sizeof ( sample ) ) )
				prepare_sample ( *app, sample );

		}

		ShowWindow ( window, SW_SHOWDEFAULT );
		UpdateWindow ( window );

		MSG message;

		while ( GetMessageW ( &message, nullptr, 0, 0 ) > 0 ) {

			TranslateMessage ( &message );
			DispatchMessageW ( &message );

		}

		if ( app->db )
			engine::close_database ( *app->db );

		ui::stop_renderer ( app->draw );

		return 0;

	}

	static void capture_draw ( void* context ) {

		draw_frame ( *static_cast < studio_state* > ( context ) );

	}

	static bool capture_view ( studio_state& app, const char* folder, const char* name ) {

		base::text path;

		path.add ( folder ).add ( '\\' ).add ( name ).add ( ".png" );

		for ( uint32_t pass = 0; pass < 2; pass++ ) {

			bool fine = ui::capture_frame ( app.draw, STUDIO_CAPTURE_WIDTH, STUDIO_CAPTURE_HEIGHT, path.value ( ), capture_draw, &app );

			app.input.pressed = false;
			app.input.released = false;
			app.input.key_count = 0;
			app.input.char_count = 0;
			app.input.wheel = 0;

			if ( !fine )
				return false;

		}

		return true;

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
		engine::close_database ( *app->db );
		ui::stop_renderer ( app->draw );
		base::erase_path ( path );
		base::erase_path ( log.value ( ) );

		return fine ? 0 : 1;

	}

	bool make_studio_copy ( ) {

		wchar_t own [ MAX_PATH ];
		DWORD size = GetModuleFileNameW ( nullptr, own, MAX_PATH );

		if ( !size || size >= MAX_PATH - 16 )
			return false;

		char path [ FILE_PATH_ROOM ];

		if ( WideCharToMultiByte ( CP_UTF8, 0, own, -1, path, sizeof ( path ), nullptr, nullptr ) <= 0 )
			return false;

		base::file_handle file;

		if ( !base::open_reading ( file, path ) )
			return false;

		uint64_t bytes = base::file_size ( file );
		uint8_t* image = base::make < uint8_t > ( static_cast < size_t > ( bytes ) );
		bool fine = image && base::read_at ( file, 0, image, static_cast < size_t > ( bytes ) );

		base::close_file ( file );

		if ( fine ) {

			uint32_t header = *reinterpret_cast < uint32_t* > ( image + 0x3C );
			uint64_t field = static_cast < uint64_t > ( header ) + 4 + 20 + 68;

			fine = field + 2 <= bytes && image [ header ] == 'P' && image [ header + 1 ] == 'E';

			if ( fine ) {

				image [ field ] = IMAGE_SUBSYSTEM_WINDOWS_GUI;
				image [ field + 1 ] = 0;

				base::text target;
				size_t cut = base::length ( path );

				while ( cut && path [ cut - 1 ] != '\\' )
					cut--;

				target.add ( path, cut ).add ( "strata-studio.exe" );

				base::file_handle out;

				fine = base::open_file ( out, target.value ( ), true );

				if ( fine ) {

					fine = base::truncate_file ( out, 0 ) && base::write_at ( out, 0, image, static_cast < size_t > ( bytes ) );
					base::close_file ( out );

				}

			}

		}

		base::release ( image );

		return fine;

	}

}
