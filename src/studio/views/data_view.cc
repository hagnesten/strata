#include "studio/app/studio.hh"
#include "sql/print/print.hh"

namespace studio {

	static void draw_stats ( studio_state& app, const ui::rect& area, const engine::table_info& table, const table_stats& stats ) {

		ui::renderer& draw = app.draw;
		engine::catalog& schema = *app.db->schema;
		float w = ( area.w - LAYOUT_MARGIN ) / 5;
		float x = area.x + LAYOUT_MARGIN;
		base::text value;
		base::text detail;

		ui::fill ( draw, area, ui::rgb ( THEME_BACKGROUND ) );
		ui::line ( draw, area.x, area.y + area.h - 0.5f, area.x + area.w, area.y + area.h - 0.5f, ui::rgb ( THEME_DIVIDER ) );

		grouped ( value, stats.rows );

		if ( app.data_grid.rows < stats.rows ) {

			detail.add ( "showing the first " );
			grouped ( detail, app.data_grid.rows );

		} else
			detail.add ( stats.rows == 1 ? "one row" : "all shown" );

		x = draw_stat ( app, x, area.y, w, "Rows", value.value ( ), detail.value ( ), true );
		value.clear ( );
		detail.clear ( );
		value.number ( table.column_count );

		if ( table.rowid_column >= 0 )
			detail.add ( "key " ).add ( table.columns [ table.rowid_column ].name );
		else if ( table.primary_count )
			detail.add ( "key " ).add ( table.columns [ table.primary [ 0 ] ].name ).add ( table.primary_count > 1 ? " and more" : "" );
		else
			detail.add ( "keyed by row id" );

		x = draw_stat ( app, x, area.y, w, "Columns", value.value ( ), detail.value ( ), true );
		value.clear ( );
		detail.clear ( );
		grouped ( value, stats.pages + stats.overflow );
		detail.number ( stats.leaves ).add ( stats.leaves == 1 ? " leaf, " : " leaves, " ).number ( stats.depth + 1 ).add ( stats.depth ? " levels" : " level" );
		x = draw_stat ( app, x, area.y, w, "Pages", value.value ( ), detail.value ( ), true );
		value.clear ( );
		detail.clear ( );
		size_text ( value, static_cast < uint64_t > ( stats.pages + stats.overflow ) * PAGE_SIZE );

		float fill = stats.pages ? static_cast < float > ( stats.used ) / static_cast < float > ( static_cast < uint64_t > ( stats.pages ) * PAGE_USABLE ) : 0;

		detail.number ( static_cast < uint64_t > ( fill * 100 + 0.5f ) ).add ( "% page fill, root page " ).number ( table.root );
		x = draw_stat ( app, x, area.y, w, "On disk", value.value ( ), detail.value ( ), true );
		value.clear ( );
		detail.clear ( );

		uint32_t indexes = 0;

		for ( uint32_t i = 0; i < schema.index_count; i++ ) {

			if ( !base::same_folded ( schema.indexes [ i ].table, table.name ) )
				continue;

			if ( indexes )
				detail.add ( ", " );

			detail.add ( schema.indexes [ i ].automatic ? "unique" : schema.indexes [ i ].name );
			indexes++;

		}

		value.number ( indexes );
		draw_stat ( app, x, area.y, w, "Indexes", value.value ( ), indexes ? detail.value ( ) : "none yet", false );

	}

	void draw_data_view ( studio_state& app, const ui::rect& area ) {

		ui::renderer& draw = app.draw;

		ui::fill ( draw, area, ui::rgb ( THEME_BACKGROUND ) );

		if ( !app.db || !app.db->open || app.table < 0 ) {

			header_action actions [ 1 ] = { { ICON_PLUS, "New table", true, app.db != nullptr } };

			if ( draw_page_header ( app, area, "Data", app.db && app.db->open ? "This database has no tables yet" : "No database is open", actions, 1 ) == 0 )
				open_designer ( app, -1 );

			ui::text ( draw, ui::FONT_UI, app.db && app.db->open ? "Make a table with New table, or import a CSV file" : "Open a database with Ctrl+O or drop a file on the window", { area.x, area.y + LAYOUT_HEADER_BAR, area.w, area.h - LAYOUT_HEADER_BAR }, ui::rgb ( THEME_FAINT ), ui::ALIGN_CENTER );

			return;

		}

		engine::table_info& table = app.db->schema->tables [ app.table ];

		ensure_stats ( app, app.table );

		const table_stats& stats = app.stats [ app.table ];
		bool row_picked = app.data_grid.selected_row >= 0 && app.data_grid.selected_row < static_cast < int32_t > ( app.data_grid.rows );
		header_action actions [ 5 ] = {

			{ ICON_IMPORT, "Import", false, true },
			{ ICON_EXPORT, "Export", false, true },
			{ ICON_EDIT, "Design", false, true },
			{ ICON_TRASH, "Delete row", false, row_picked },
			{ ICON_PLUS, "Add row", true, true },

		};
		base::text subtitle;

		subtitle.add ( "Double-click a cell to change it   Delete removes the selected row" );

		switch ( draw_page_header ( app, area, table.name, subtitle.value ( ), actions, 5 ) ) {

			case 0:
				open_import ( app );
				break;

			case 1: {

				base::text sql;

				sql.add ( "SELECT * FROM " );
				sql::print_name ( sql, table.name );
				open_export ( app, sql.value ( ), table.name );

				break;

			}

			case 2:
				open_designer ( app, app.table );
				break;

			case 3:
				delete_table_row ( app );
				break;

			case 4:
				add_table_row ( app );
				break;

			default:
				break;

		}

		if ( app.table < 0 || !app.db->schema->table_count )
			return;

		float y = area.y + LAYOUT_HEADER_BAR;

		draw_stats ( app, { area.x, y, area.w, LAYOUT_STATS }, app.db->schema->tables [ app.table ], app.stats [ app.table ] );
		y += LAYOUT_STATS;

		if ( app.db->in_transaction ) {

			draw_change_bar ( app, { area.x, y, area.w, CHANGE_BAR } );
			y += CHANGE_BAR;

		}

		ui::rect grid = { area.x, y, area.w, area.y + area.h - y };

		draw_grid ( app.ui, app.data_grid, grid, stats.rows ? "Loading rows" : "This table is empty. Add a row to start." );

		if ( app.data_grid.activated ) {

			app.data_grid.activated = false;

			if ( app.data_grid.selected_row >= 0 && app.data_grid.selected_column >= static_cast < int32_t > ( app.data_grid.hidden ) )
				begin_cell_edit ( app, static_cast < uint32_t > ( app.data_grid.selected_row ), static_cast < uint32_t > ( app.data_grid.selected_column ) );

		}

		if ( app.data_grid.erase ) {

			app.data_grid.erase = false;
			delete_table_row ( app );

		}

		draw_cell_edit ( app, grid );

	}

}