#include "studio/app/studio.hh"
#include "sql/print/print.hh"

#include <windows.h>

namespace studio {

	bool row_rowid ( studio_state& app, uint32_t row, int64_t& rowid ) {

		grid_data& grid = app.data_grid;

		if ( row >= grid.rows || !grid.column_count )
			return false;

		const char* text = grid_cell ( grid, row, 0 );
		bool negative = *text == '-';
		uint64_t value = 0;

		if ( !base::read_number ( negative ? text + 1 : text, value ) )
			return false;

		rowid = negative ? -static_cast < int64_t > ( value ) : static_cast < int64_t > ( value );

		return true;

	}

	static bool open_changes ( studio_state& app ) {

		return app.db->in_transaction || run_sql ( app, "BEGIN", nullptr, 0 );

	}

	static engine::table_info* edited_table ( studio_state& app ) {

		if ( !app.db || app.table < 0 || app.table >= static_cast < int32_t > ( app.db->schema->table_count ) )
			return nullptr;

		return &app.db->schema->tables [ app.table ];

	}

	static int32_t find_row ( studio_state& app, int64_t rowid ) {

		for ( uint32_t r = 0; r < app.data_grid.rows; r++ ) {

			int64_t here = 0;

			if ( row_rowid ( app, r, here ) && here == rowid )
				return static_cast < int32_t > ( r );

		}

		return -1;

	}

	void begin_cell_edit ( studio_state& app, uint32_t row, uint32_t column ) {

		grid_data& grid = app.data_grid;
		cell_edit& edit = app.edit;

		if ( row >= grid.rows || column < grid.hidden || column >= grid.column_count || !row_rowid ( app, row, edit.rowid ) )
			return;

		uint8_t kind = grid_kind ( grid, row, column );

		if ( kind == storage::VALUE_BLOB ) {

			set_status ( app, "Blob values can be changed with UPDATE in the Query view", true );

			return;

		}

		edit.active = true;
		edit.row = row;
		edit.column = column;
		edit.was_null = kind == storage::VALUE_NULL;
		edit.focus_pending = true;
		set_field ( edit.field, edit.was_null ? "" : grid_cell ( grid, row, column ) );

	}

	void cancel_cell_edit ( studio_state& app ) {

		app.edit.active = false;
		app.ui.focus = app.data_grid.id;

	}

	bool commit_cell_edit ( studio_state& app, bool null_value ) {

		cell_edit& edit = app.edit;
		engine::table_info* table = edited_table ( app );

		if ( !edit.active || !table )
			return false;

		const char* original = grid_cell ( app.data_grid, edit.row, edit.column );
		bool unchanged = null_value ? edit.was_null : !edit.was_null && base::same ( original, edit.field.text.value ( ) );

		if ( unchanged ) {

			cancel_cell_edit ( app );

			return true;

		}

		if ( !open_changes ( app ) )
			return false;

		const char* column = table->columns [ edit.column - app.data_grid.hidden ].name;
		base::text sql;
		storage::value parameters [ 2 ];

		sql.add ( "UPDATE " );
		sql::print_name ( sql, table->name );
		sql.add ( " SET " );
		sql::print_name ( sql, column );
		sql.add ( " = ? WHERE rowid = ?" );
		parameters [ 0 ] = null_value ? storage::null_value ( ) : storage::text_value ( edit.field.text.value ( ), edit.field.text.size );
		parameters [ 1 ] = storage::integer_value ( edit.rowid );

		if ( !run_sql ( app, sql.value ( ), parameters, 2 ) )
			return false;

		int64_t rowid = edit.rowid;
		uint32_t at_column = edit.column;

		edit.active = false;
		reload_table ( app );

		int32_t row = find_row ( app, rowid );

		if ( row >= 0 ) {

			app.data_grid.selected_row = row;
			app.data_grid.selected_column = static_cast < int32_t > ( at_column );

		}

		base::text message;

		message.add ( "Changed " ).add ( column ).add ( null_value ? " to NULL" : "" ).add ( ". Save to keep it, Discard to undo." );
		set_status ( app, message.value ( ), false );
		app.ui.focus = app.data_grid.id;

		return true;

	}

	void add_table_row ( studio_state& app ) {

		engine::table_info* table = edited_table ( app );

		if ( !table || !open_changes ( app ) )
			return;

		base::text sql;

		sql.add ( "INSERT INTO " );
		sql::print_name ( sql, table->name );
		sql.add ( " DEFAULT VALUES" );

		if ( !engine::execute ( *app.db, sql.value ( ), nullptr, 0, nullptr ) ) {

			sql.clear ( );
			sql.add ( "INSERT INTO " );
			sql::print_name ( sql, table->name );
			sql.add ( " (" );

			bool any = false;
			base::text values;

			for ( uint32_t c = 0; c < table->column_count; c++ ) {

				const engine::column_info& info = table->columns [ c ];

				if ( !info.not_null || info.fallback || info.generated || info.rowid )
					continue;

				sql.add ( any ? ", " : " " );
				sql::print_name ( sql, info.name );
				values.add ( any ? ", " : " " ).add ( info.affinity == sql::AFFINITY_TEXT || info.affinity == sql::AFFINITY_ANY ? "''" : "0" );
				any = true;

			}

			sql.add ( " ) VALUES (" ).add ( values.value ( ) ).add ( " )" );

			if ( !any || !run_sql ( app, sql.value ( ), nullptr, 0 ) ) {

				if ( !any )
					set_status ( app, app.db->problem.value ( ), true );

				return;

			}

		}

		int64_t rowid = app.db->last_rowid;

		reload_table ( app );

		int32_t row = find_row ( app, rowid );

		if ( row < 0 ) {

			set_status ( app, "Added a row past the rows shown here", false );

			return;

		}

		app.data_grid.selected_row = row;
		app.data_grid.selected_column = static_cast < int32_t > ( app.data_grid.hidden );
		app.data_grid.scroll_y = static_cast < float > ( row ) * LAYOUT_ROW;
		set_status ( app, "Added a row. Save to keep it, Discard to undo.", false );

		uint32_t first = app.data_grid.hidden;

		for ( uint32_t c = 0; c < table->column_count; c++ ) {

			if ( !table->columns [ c ].rowid && !table->columns [ c ].generated ) {

				first = c + app.data_grid.hidden;

				break;

			}

		}

		app.data_grid.selected_column = static_cast < int32_t > ( first );
		begin_cell_edit ( app, static_cast < uint32_t > ( row ), first );

	}

	void delete_table_row ( studio_state& app ) {

		engine::table_info* table = edited_table ( app );
		int64_t rowid = 0;
		int32_t row = app.data_grid.selected_row;

		if ( !table || row < 0 || !row_rowid ( app, static_cast < uint32_t > ( row ), rowid ) || !open_changes ( app ) )
			return;

		base::text sql;
		storage::value parameter = storage::integer_value ( rowid );

		sql.add ( "DELETE FROM " );
		sql::print_name ( sql, table->name );
		sql.add ( " WHERE rowid = ?" );

		if ( !run_sql ( app, sql.value ( ), &parameter, 1 ) )
			return;

		app.edit.active = false;
		reload_table ( app );
		app.data_grid.selected_row = row < static_cast < int32_t > ( app.data_grid.rows ) ? row : static_cast < int32_t > ( app.data_grid.rows ) - 1;
		set_status ( app, "Deleted the row. Save to keep it, Discard to undo.", false );

	}

	void finish_changes ( studio_state& app, bool keep ) {

		if ( !app.db || !app.db->in_transaction )
			return;

		app.edit.active = false;

		if ( !run_sql ( app, keep ? "COMMIT" : "ROLLBACK", nullptr, 0 ) )
			return;

		after_change ( app );
		set_status ( app, keep ? "Saved the changes to the file" : "Discarded the changes", false );

	}

	void draw_change_bar ( studio_state& app, const ui::rect& area ) {

		ui_context& ui = app.ui;
		ui::renderer& draw = app.draw;

		ui::fill ( draw, area, ui::rgb ( THEME_PANEL ) );
		ui::fill ( draw, { area.x, area.y, 3, area.h }, ui::rgb ( THEME_WARNING ) );
		ui::line ( draw, area.x, area.y + area.h - 0.5f, area.x + area.w, area.y + area.h - 0.5f, ui::rgb ( THEME_DIVIDER ) );
		ui::text ( draw, ui::FONT_STRONG, "Unsaved changes", { area.x + LAYOUT_MARGIN, area.y, 160, area.h }, ui::rgb ( THEME_TEXT ) );
		ui::text ( draw, ui::FONT_SMALL, "They live in an open transaction until you save or discard them", { area.x + LAYOUT_MARGIN + 140, area.y, area.w - 480, area.h }, ui::rgb ( THEME_DIM ) );

		float save_w = icon_button_width ( draw, "Save" );
		float discard_w = icon_button_width ( draw, "Discard" );
		float right = area.x + area.w - LAYOUT_MARGIN;

		if ( icon_button ( ui, make_id ( "changes.save" ), { right - save_w, area.y + 6, save_w, area.h - 12 }, ICON_SAVE, "Save", true ) )
			finish_changes ( app, true );

		if ( icon_button ( ui, make_id ( "changes.discard" ), { right - save_w - 10 - discard_w, area.y + 6, discard_w, area.h - 12 }, ICON_REFRESH, "Discard" ) )
			finish_changes ( app, false );

	}

	void draw_cell_edit ( studio_state& app, const ui::rect& grid_area ) {

		ui_context& ui = app.ui;
		ui::renderer& draw = app.draw;
		cell_edit& edit = app.edit;
		ui::rect cell;

		if ( !edit.active )
			return;

		if ( !grid_cell_rect ( app.data_grid, draw, grid_area, edit.row, edit.column, cell ) ) {

			edit.active = false;

			return;

		}

		ui::rect box = { cell.x - 1, cell.y - 1, base::max ( cell.w + 2, 180.0f ), cell.h + 2 };

		if ( cell.y < grid_area.y + LAYOUT_HEADER - 0.5f || cell.y + cell.h > grid_area.y + grid_area.h - LAYOUT_SCROLLBAR ) {

			app.data_grid.scroll_y = static_cast < float > ( edit.row ) * LAYOUT_ROW;

			return;

		}

		if ( edit.focus_pending ) {

			focus_field ( ui, edit.field );
			edit.field.anchor = 0;
			edit.field.caret = edit.field.text.size;
			edit.focus_pending = false;

		}

		bool set_null = ui.focus == edit.field.id && app.input.ctrl && key_hit ( ui, 'N' );
		bool outside = app.input.pressed && !ui::inside ( box, app.input.x, app.input.y );
		field_event event = text_box ( ui, edit.field, box, edit.was_null ? "NULL" : nullptr );
		ui::rect hint = { box.x, box.y + box.h + 4, 330, 26 };

		ui::fill_round ( draw, hint, LAYOUT_RADIUS, ui::rgb ( THEME_RAISED ) );
		ui::outline_round ( draw, hint, LAYOUT_RADIUS, ui::rgb ( THEME_BORDER ) );
		ui::text ( draw, ui::FONT_SMALL, "Enter saves   Esc cancels   Ctrl+N sets NULL", { hint.x + 10, hint.y, hint.w - 20, hint.h }, ui::rgb ( THEME_DIM ) );

		if ( set_null )
			commit_cell_edit ( app, true );
		else if ( event == FIELD_SUBMIT || outside )
			commit_cell_edit ( app, false );
		else if ( event == FIELD_CANCEL )
			cancel_cell_edit ( app );

	}

}