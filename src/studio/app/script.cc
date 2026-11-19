#include "studio/app/studio.hh"

#include <windows.h>

namespace studio {

	static ui::rect content_area ( studio_state& app ) {

		float width = static_cast < float > ( STUDIO_CAPTURE_WIDTH );
		float height = static_cast < float > ( STUDIO_CAPTURE_HEIGHT );
		float sidebar = clamp ( app.sidebar_width, 200, width * 0.4f );

		return { sidebar + 1, LAYOUT_TITLE, width - sidebar - 1, height - LAYOUT_TITLE - LAYOUT_STATUS };

	}

	static void click_action ( studio_state& app, const char* const* labels, uint32_t count, uint32_t index ) {

		ui::rect area = content_area ( app );
		float x = area.x + area.w - LAYOUT_MARGIN;

		for ( uint32_t i = count; i-- > 0; ) {

			float w = icon_button_width ( app.draw, labels [ i ] );

			if ( i == index ) {

				script_click ( app, x - w * 0.5f, area.y + LAYOUT_HEADER_BAR * 0.5f );

				return;

			}

			x -= w + 10;

		}

	}

	static ui::rect data_grid_area ( studio_state& app ) {

		ui::rect area = content_area ( app );
		float y = area.y + LAYOUT_HEADER_BAR + LAYOUT_STATS + ( app.db->in_transaction ? CHANGE_BAR : 0 );

		return { area.x, y, area.w, area.y + area.h - y };

	}

	static bool editor_holds ( studio_state& app, const char* part ) {

		size_t size = base::length ( part );

		for ( size_t i = 0; i + size <= app.editor.text.size; i++ )
			if ( base::same_bytes ( app.editor.text.value ( ) + i, part, size ) )
				return true;

		return false;

	}

	static int32_t table_index ( studio_state& app, const char* name ) {

		for ( uint32_t t = 0; t < app.db->schema->table_count; t++ )
			if ( base::same ( app.db->schema->tables [ t ].name, name ) )
				return static_cast < int32_t > ( t );

		return -1;

	}

	static void play_editing ( studio_state& app, const char* folder, script_check& check ) {

		static const char* const data_actions [ 5 ] = { "Import", "Export", "Design", "Delete row", "Add row" };

		script_key ( app, '1', true );
		select_table ( app, table_index ( app, "customers" ) );
		script_frame ( app );

		ui::rect grid = data_grid_area ( app );
		ui::rect cell;
		uint32_t name_column = 0;

		for ( uint32_t c = 0; c < app.data_grid.column_count; c++ )
			if ( base::same ( app.data_grid.columns [ c ].name, "name" ) )
				name_column = c;

		script_step ( check, grid_cell_rect ( app.data_grid, app.draw, grid, 0, name_column, cell ), "the name cell is on screen" );
		script_double_click ( app, cell.x + 20, cell.y + cell.h * 0.5f );
		script_step ( check, app.edit.active, "double-clicking a cell starts editing" );
		script_key ( app, 'A', true );
		script_type ( app, "Ada Lovelace" );
		script_key ( app, VK_RETURN, false );
		script_step ( check, app.db->in_transaction && base::same ( grid_cell ( app.data_grid, 0, name_column ), "Ada Lovelace" ), "an edited cell is written in a transaction" );
		script_saved ( check, script_shot ( app, folder, "script-edit" ) );

		uint64_t rows = app.stats [ app.table ].rows;

		click_action ( app, data_actions, 5, 4 );
		script_key ( app, VK_ESCAPE, false );
		script_step ( check, app.stats [ app.table ].rows == rows + 1, "Add row adds a row" );

		ui::rect bar = { content_area ( app ).x, content_area ( app ).y + LAYOUT_HEADER_BAR + LAYOUT_STATS, content_area ( app ).w, CHANGE_BAR };
		float save_w = icon_button_width ( app.draw, "Save" );
		float discard_w = icon_button_width ( app.draw, "Discard" );

		script_click ( app, bar.x + bar.w - LAYOUT_MARGIN - save_w - 10 - discard_w * 0.5f, bar.y + bar.h * 0.5f );
		script_step ( check, !app.db->in_transaction && app.stats [ app.table ].rows == rows && !base::same ( grid_cell ( app.data_grid, 0, name_column ), "Ada Lovelace" ), "Discard rolls the edits back" );

	}

	static void play_query ( studio_state& app, const char* folder, script_check& check ) {

		static const char* const query_actions [ 5 ] = { "History", "Save", "Export", "Explain", "Run" };
		ui::rect area = content_area ( app );

		script_key ( app, '2', true );
		script_key ( app, 'T', true );
		script_step ( check, app.tab_count == 2 && !app.editor.text.size, "Ctrl+T opens an empty query tab" );
		script_click ( app, area.x + 400, area.y + LAYOUT_HEADER_BAR + QUERY_TABS_HEIGHT + 60 );
		script_type ( app, "SELECT city, count(*) AS people FROM cu" );
		script_step ( check, app.editor.complete.open, "typing a table prefix opens suggestions" );
		script_saved ( check, script_shot ( app, folder, "script-complete" ) );
		script_key ( app, VK_TAB, false );
		script_step ( check, editor_holds ( app, "FROM customers" ), "Tab accepts a suggestion" );
		script_type ( app, " GROUP BY city ORDER BY people DESC" );
		click_action ( app, query_actions, 5, 4 );
		script_step ( check, app.result == RESULT_GRID && app.result_grid.rows == 12, "the new tab runs its query" );

		float strip_y = area.y + LAYOUT_HEADER_BAR + QUERY_TABS_HEIGHT + clamp ( app.editor_height, 90, area.h - LAYOUT_HEADER_BAR - QUERY_TABS_HEIGHT - 160 );
		float right = area.x + area.w - 14 - ( app.library.page != LIBRARY_HIDDEN ? LIBRARY_WIDTH : 0 );
		uint32_t segments = 2 + ( app.plan.count ? 1 : 0 );

		script_click ( app, right - static_cast < float > ( segments ) * 78 + 78 + 39, strip_y + 21 );
		script_step ( check, app.result == RESULT_CHART, "the Chart button shows a chart" );
		script_saved ( check, script_shot ( app, folder, "script-chart" ) );
		click_action ( app, query_actions, 5, 0 );
		script_step ( check, app.library.page == LIBRARY_HISTORY && app.library.history_count >= 2, "History lists the queries that ran" );
		script_saved ( check, script_shot ( app, folder, "script-history" ) );
		click_action ( app, query_actions, 5, 0 );
		script_click ( app, area.x + 20, area.y + LAYOUT_HEADER_BAR + QUERY_TABS_HEIGHT * 0.5f );
		script_step ( check, app.tab == 0, "clicking the first tab switches back" );
		click_action ( app, query_actions, 5, 2 );
		script_step ( check, app.overlay == OVERLAY_TRANSFER && app.transfer.mode == TRANSFER_EXPORT, "Export opens the export dialog" );
		script_saved ( check, script_shot ( app, folder, "script-export" ) );
		script_key ( app, VK_ESCAPE, false );

	}

	static void play_dialogs ( studio_state& app, const char* folder, script_check& check ) {

		static const char* const data_actions [ 5 ] = { "Import", "Export", "Design", "Delete row", "Add row" };
		static const char* const pages_actions [ 1 ] = { "Check file" };

		script_key ( app, 'O', true );
		script_step ( check, app.overlay == OVERLAY_OPEN, "Ctrl+O opens the file picker" );
		script_saved ( check, script_shot ( app, folder, "script-open" ) );
		script_key ( app, VK_ESCAPE, false );
		script_step ( check, app.overlay == OVERLAY_NONE, "Escape closes a dialog" );
		script_key ( app, '1', true );
		click_action ( app, data_actions, 5, 2 );
		script_step ( check, app.overlay == OVERLAY_DESIGN && app.design.count == app.db->schema->tables [ app.table ].column_count, "Design shows the table's columns" );
		script_saved ( check, script_shot ( app, folder, "script-design" ) );
		script_key ( app, VK_ESCAPE, false );
		click_action ( app, data_actions, 5, 0 );

		const char* sample = "name,score,joined\r\nAda,91.5,2024-01-02\r\n\"Lovelace, A\",88,2024-02-03\r\nGrace,,2024-03-04\r\n";

		app.transfer.source.clear ( );
		app.transfer.source.add ( sample );
		app.transfer.parsed = parse_csv ( app.transfer.source.value ( ), app.transfer.source.size, app.transfer.preview, app.transfer.problem );
		set_field ( app.transfer.table, "scores" );
		script_step ( check, app.transfer.parsed && app.transfer.preview.rows == 3 && app.transfer.preview.types [ 1 ] == sql::AFFINITY_REAL && !app.transfer.preview.cells [ 7 ], "a pasted CSV is read with its types" );
		script_saved ( check, script_shot ( app, folder, "script-import" ) );
		script_key ( app, VK_ESCAPE, false );
		script_key ( app, '4', true );
		click_action ( app, pages_actions, 1, 0 );
		script_step ( check, app.overlay == OVERLAY_CHECK && app.check.done && !app.check.problem_count, "Check file finds no problems" );
		script_saved ( check, script_shot ( app, folder, "script-check" ) );
		script_key ( app, VK_ESCAPE, false );

	}

	bool play_features ( studio_state& app, const char* folder, script_check& check ) {

		play_editing ( app, folder, check );
		play_query ( app, folder, check );
		play_dialogs ( app, folder, check );

		return check.fine;

	}

}