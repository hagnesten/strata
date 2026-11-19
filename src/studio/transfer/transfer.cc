#include "studio/app/studio.hh"
#include "sql/print/print.hh"
#include "base/clipboard/clipboard.hh"

namespace studio {

	static bool export_columns ( void* context, const char* const* names, uint32_t count ) {

		export_state& state = *static_cast < export_state* > ( context );

		state.names = state.memory.make < const char* > ( count + 1 );
		state.name_count = count;

		if ( !state.names )
			return false;

		for ( uint32_t c = 0; c < count; c++ )
			state.names [ c ] = state.memory.keep ( names [ c ], base::length ( names [ c ] ) );

		if ( state.format == FORMAT_CSV )
			write_csv_header ( state.out, state.names, count );
		else
			state.out.add ( '[' );

		return true;

	}

	static bool export_row ( void* context, const storage::value* values, uint32_t count ) {

		export_state& state = *static_cast < export_state* > ( context );

		if ( state.format == FORMAT_CSV )
			write_csv_row ( state.out, values, count );
		else
			write_json_row ( state.out, state.names, values, count, !state.rows );

		state.rows++;

		return true;

	}

	bool export_rows ( engine::database& db, const char* sql, transfer_format format, const char* path, uint64_t& rows, base::text& problem ) {

		export_state* state = base::make < export_state > ( 1 );

		if ( !state ) {

			problem.add ( "Out of memory" );

			return false;

		}

		state->format = format;

		engine::query_sink sink = { state, export_columns, export_row };
		bool fine = engine::execute ( db, sql, nullptr, 0, &sink );

		if ( !fine )
			problem.add ( db.problem.value ( ) );

		if ( fine && format == FORMAT_JSON )
			state->out.add ( state->rows ? "\n]\n" : "]\n" );

		if ( fine && !base::write_whole ( path, state->out.value ( ), state->out.size ) ) {

			problem.add ( "The file could not be written" );
			fine = false;

		}

		rows = state->rows;
		state->out.~text ( );
		state->memory.~arena ( );
		base::release ( state );

		return fine;

	}

	bool import_rows ( engine::database& db, const char* name, const import_table& table, base::text& problem ) {

		static const char* const types [ 6 ] = { "", "INTEGER", "REAL", "NUMERIC", "TEXT", "BLOB" };
		bool began = !db.in_transaction;
		base::text sql;

		if ( began && !engine::execute ( db, "BEGIN", nullptr, 0, nullptr ) ) {

			problem.add ( db.problem.value ( ) );

			return false;

		}

		sql.add ( "CREATE TABLE " );
		sql::print_name ( sql, name );
		sql.add ( " (" );

		for ( uint32_t c = 0; c < table.column_count; c++ ) {

			sql.add ( c ? ", " : " " );
			sql::print_name ( sql, table.names [ c ] );
			sql.add ( ' ' ).add ( types [ table.types ? table.types [ c ] : sql::AFFINITY_TEXT ] );

		}

		sql.add ( " )" );

		bool fine = engine::execute ( db, sql.value ( ), nullptr, 0, nullptr );

		sql.clear ( );
		sql.add ( "INSERT INTO " );
		sql::print_name ( sql, name );
		sql.add ( " VALUES (" );

		for ( uint32_t c = 0; c < table.column_count; c++ )
			sql.add ( c ? ", ?" : " ?" );

		sql.add ( " )" );

		storage::value* values = base::make < storage::value > ( table.column_count + 1 );

		fine = fine && values;

		for ( uint32_t r = 0; r < table.rows && fine; r++ ) {

			for ( uint32_t c = 0; c < table.column_count; c++ ) {

				const char* cell = table.cells [ static_cast < size_t > ( r ) * table.column_count + c ];

				values [ c ] = cell ? storage::text_value ( cell, base::length ( cell ) ) : storage::null_value ( );

			}

			fine = engine::execute ( db, sql.value ( ), values, table.column_count, nullptr );

		}

		base::release ( values );

		if ( !fine ) {

			problem.add ( db.problem.size ? db.problem.value ( ) : "Out of memory" );

			if ( began )
				engine::execute ( db, "ROLLBACK", nullptr, 0, nullptr );

			return false;

		}

		if ( began && !engine::execute ( db, "COMMIT", nullptr, 0, nullptr ) ) {

			problem.add ( db.problem.value ( ) );

			return false;

		}

		return true;

	}

	static void export_path ( studio_state& app, const char* name ) {

		base::text path;

		documents_folder ( path );
		path.add ( '\\' );

		for ( const char* at = name; *at; at++ )
			path.add ( *at == '\\' || *at == '/' || *at == ':' ? '_' : *at );

		path.add ( app.transfer.format == FORMAT_CSV ? ".csv" : ".json" );
		set_field ( app.transfer.path, path.value ( ) );

	}

	void open_export ( studio_state& app, const char* sql, const char* name ) {

		transfer_state& transfer = app.transfer;

		transfer.mode = TRANSFER_EXPORT;
		transfer.sql.clear ( );
		transfer.sql.add ( sql );
		transfer.problem.clear ( );
		set_field ( transfer.table, name );
		export_path ( app, name );
		app.overlay = OVERLAY_TRANSFER;

	}

	void open_import ( studio_state& app ) {

		transfer_state& transfer = app.transfer;
		base::text path;

		transfer.mode = TRANSFER_IMPORT;
		transfer.parsed = false;
		transfer.problem.clear ( );
		transfer.source.clear ( );
		documents_folder ( path );
		path.add ( "\\" );
		set_field ( transfer.path, path.value ( ) );
		set_field ( transfer.table, "imported" );
		app.overlay = OVERLAY_TRANSFER;

	}

	static void read_source ( studio_state& app ) {

		transfer_state& transfer = app.transfer;
		const char* text = transfer.source.value ( );
		size_t at = 0;

		transfer.problem.clear ( );

		while ( at < transfer.source.size && ( text [ at ] == ' ' || text [ at ] == '\n' || text [ at ] == '\r' || text [ at ] == '\t' ) )
			at++;

		transfer.format = at < transfer.source.size && ( text [ at ] == '[' || text [ at ] == '{' ) ? FORMAT_JSON : FORMAT_CSV;
		transfer.parsed = transfer.format == FORMAT_JSON ? parse_json_rows ( text, transfer.source.size, transfer.preview, transfer.problem ) : parse_csv ( text, transfer.source.size, transfer.preview, transfer.problem );

	}

	static void load_file ( studio_state& app ) {

		transfer_state& transfer = app.transfer;

		transfer.from_clipboard = false;

		if ( !base::read_whole ( transfer.path.text.value ( ), transfer.source ) ) {

			transfer.parsed = false;
			transfer.problem.clear ( );
			transfer.problem.add ( "That file could not be read" );

			return;

		}

		base::text name;

		file_name_of ( transfer.path.text.value ( ), name );

		for ( size_t i = name.size; i > 0; i-- ) {

			if ( name.data [ i - 1 ] == '.' ) {

				name.data [ i - 1 ] = 0;
				name.size = i - 1;

				break;

			}

		}

		for ( size_t i = 0; i < name.size; i++ )
			if ( !editor_word_char ( name.data [ i ] ) )
				name.data [ i ] = '_';

		if ( name.size )
			set_field ( transfer.table, name.value ( ) );

		read_source ( app );

	}

	static void draw_preview ( studio_state& app, const ui::rect& area ) {

		ui::renderer& draw = app.draw;
		import_table& table = app.transfer.preview;
		static const char* const types [ 6 ] = { "any", "integer", "real", "numeric", "text", "blob" };

		ui::fill_round ( draw, area, LAYOUT_RADIUS, ui::rgb ( THEME_BACKGROUND ) );
		ui::outline_round ( draw, area, LAYOUT_RADIUS, ui::rgb ( THEME_DIVIDER ) );

		if ( !app.transfer.parsed ) {

			ui::text ( draw, ui::FONT_UI, app.transfer.problem.size ? app.transfer.problem.value ( ) : "Load a file or paste rows to see them here", area, ui::rgb ( app.transfer.problem.size ? THEME_ERROR : THEME_FAINT ), ui::ALIGN_CENTER );

			return;

		}

		ui::push_clip ( draw, inset ( area, 1 ) );

		uint32_t shown = table.column_count < 8 ? table.column_count : 8;
		float column_w = ( area.w - 24 ) / static_cast < float > ( shown ? shown : 1 );
		float y = area.y + 10;

		for ( uint32_t c = 0; c < shown; c++ ) {

			float x = area.x + 12 + static_cast < float > ( c ) * column_w;

			ui::text ( draw, ui::FONT_STRONG, table.names [ c ], { x, y, column_w - 10, 22 }, ui::rgb ( THEME_TEXT ) );
			ui::text ( draw, ui::FONT_SMALL, types [ table.types ? table.types [ c ] : 4 ], { x, y + 20, column_w - 10, 18 }, ui::rgb ( THEME_ACCENT ) );

		}

		y += 46;
		ui::line ( draw, area.x, y - 0.5f, area.x + area.w, y - 0.5f, ui::rgb ( THEME_DIVIDER ) );

		for ( uint32_t r = 0; r < table.rows && r < IMPORT_PREVIEW; r++ ) {

			for ( uint32_t c = 0; c < shown; c++ ) {

				const char* cell = table.cells [ static_cast < size_t > ( r ) * table.column_count + c ];

				ui::text ( draw, ui::FONT_UI, cell ? cell : "NULL", { area.x + 12 + static_cast < float > ( c ) * column_w, y + 4, column_w - 10, 24 }, ui::rgb ( cell ? THEME_TEXT : THEME_FAINT ) );

			}

			y += 28;

		}

		ui::pop_clip ( draw );

	}

	void draw_transfer_overlay ( studio_state& app, const ui::rect& area ) {

		ui_context& ui = app.ui;
		ui::renderer& draw = app.draw;
		transfer_state& transfer = app.transfer;
		bool exporting = transfer.mode == TRANSFER_EXPORT;
		ui::rect inner = draw_overlay_frame ( app, area, 820, exporting ? 340 : 620, exporting ? "Export rows" : "Import rows" );
		float y = inner.y;
		float label_w = 130;

		if ( exporting ) {

			ui::text ( draw, ui::FONT_UI, "Format", { inner.x, y, label_w, LAYOUT_BUTTON }, ui::rgb ( THEME_DIM ) );

			if ( segment ( ui, make_id ( "export.csv" ), { inner.x + label_w, y, 110, LAYOUT_BUTTON }, "CSV", transfer.format == FORMAT_CSV, true, false ) ) {

				transfer.format = FORMAT_CSV;
				export_path ( app, transfer.table.text.value ( ) );

			}

			if ( segment ( ui, make_id ( "export.json" ), { inner.x + label_w + 110, y, 110, LAYOUT_BUTTON }, "JSON", transfer.format == FORMAT_JSON, false, true ) ) {

				transfer.format = FORMAT_JSON;
				export_path ( app, transfer.table.text.value ( ) );

			}

			y += LAYOUT_BUTTON + 14;
			ui::text ( draw, ui::FONT_UI, "Save to", { inner.x, y, label_w, LAYOUT_BUTTON }, ui::rgb ( THEME_DIM ) );

			field_event event = text_box ( ui, transfer.path, { inner.x + label_w, y, inner.w - label_w, LAYOUT_BUTTON }, "File path" );

			y += LAYOUT_BUTTON + 14;
			ui::text ( draw, ui::FONT_SMALL, "Every row of the query is written, not only the rows on screen", { inner.x + label_w, y, inner.w - label_w, 20 }, ui::rgb ( THEME_FAINT ) );

			if ( transfer.problem.size )
				ui::text ( draw, ui::FONT_SMALL, transfer.problem.value ( ), transfer.problem.size, { inner.x + label_w, y + 22, inner.w - label_w, 20 }, ui::rgb ( THEME_ERROR ) );

			float go_w = icon_button_width ( draw, "Export" );
			bool go = icon_button ( ui, make_id ( "export.go" ), { inner.x + inner.w - go_w, inner.y + inner.h - LAYOUT_BUTTON, go_w, LAYOUT_BUTTON }, ICON_EXPORT, "Export", true, transfer.path.text.size > 0 && app.db );

			if ( ( go || event == FIELD_SUBMIT ) && app.db ) {

				uint64_t rows = 0;

				transfer.problem.clear ( );

				if ( export_rows ( *app.db, transfer.sql.value ( ), transfer.format, transfer.path.text.value ( ), rows, transfer.problem ) ) {

					base::text message;

					message.add ( "Wrote " );
					grouped ( message, rows );
					message.add ( rows == 1 ? " row to " : " rows to " ).add ( transfer.path.text.value ( ) );
					set_status ( app, message.value ( ), false );
					app.overlay = OVERLAY_NONE;

				}

			}

			return;

		}

		ui::text ( draw, ui::FONT_UI, "From a file", { inner.x, y, label_w, LAYOUT_BUTTON }, ui::rgb ( THEME_DIM ) );

		float load_w = icon_button_width ( draw, "Load" );
		float paste_w = icon_button_width ( draw, "Paste rows" );

		if ( text_box ( ui, transfer.path, { inner.x + label_w, y, inner.w - label_w - load_w - paste_w - 20, LAYOUT_BUTTON }, "C:\\path\\to\\rows.csv" ) == FIELD_SUBMIT )
			load_file ( app );

		if ( icon_button ( ui, make_id ( "import.load" ), { inner.x + inner.w - load_w - paste_w - 10, y, load_w, LAYOUT_BUTTON }, ICON_OPEN, "Load" ) )
			load_file ( app );

		if ( icon_button ( ui, make_id ( "import.paste" ), { inner.x + inner.w - paste_w, y, paste_w, LAYOUT_BUTTON }, ICON_IMPORT, "Paste rows" ) ) {

			transfer.from_clipboard = true;

			if ( base::paste_from_clipboard ( transfer.source ) )
				read_source ( app );
			else {

				transfer.parsed = false;
				transfer.problem.clear ( );
				transfer.problem.add ( "The clipboard holds no text" );

			}

		}

		y += LAYOUT_BUTTON + 14;
		ui::text ( draw, ui::FONT_UI, "New table", { inner.x, y, label_w, LAYOUT_BUTTON }, ui::rgb ( THEME_DIM ) );
		text_box ( ui, transfer.table, { inner.x + label_w, y, 320, LAYOUT_BUTTON }, "table name" );

		if ( transfer.parsed ) {

			base::text summary;

			grouped ( summary, transfer.preview.rows );
			summary.add ( transfer.preview.rows == 1 ? " row, " : " rows, " ).number ( transfer.preview.column_count ).add ( transfer.preview.column_count == 1 ? " column from " : " columns from " ).add ( transfer.format == FORMAT_JSON ? "JSON" : "CSV" );
			ui::text ( draw, ui::FONT_SMALL, summary.value ( ), summary.size, { inner.x + label_w + 336, y, inner.w - label_w - 336, LAYOUT_BUTTON }, ui::rgb ( THEME_DIM ) );

		}

		y += LAYOUT_BUTTON + 18;
		draw_preview ( app, { inner.x, y, inner.w, inner.y + inner.h - y - LAYOUT_BUTTON - 18 } );

		float go_w = icon_button_width ( draw, "Import" );
		bool exists = app.db && engine::find_table ( *app.db->schema, transfer.table.text.value ( ) );

		if ( exists )
			ui::text ( draw, ui::FONT_SMALL, "A table with that name already exists", { inner.x, inner.y + inner.h - LAYOUT_BUTTON, inner.w - go_w - 20, LAYOUT_BUTTON }, ui::rgb ( THEME_ERROR ) );

		if ( icon_button ( ui, make_id ( "import.go" ), { inner.x + inner.w - go_w, inner.y + inner.h - LAYOUT_BUTTON, go_w, LAYOUT_BUTTON }, ICON_IMPORT, "Import", true, transfer.parsed && transfer.table.text.size && !exists && app.db ) ) {

			transfer.problem.clear ( );

			if ( import_rows ( *app.db, transfer.table.text.value ( ), transfer.preview, transfer.problem ) ) {

				base::text message;

				message.add ( "Imported " );
				grouped ( message, transfer.preview.rows );
				message.add ( transfer.preview.rows == 1 ? " row into " : " rows into " ).add ( transfer.table.text.value ( ) );
				refresh_schema ( app );

				for ( uint32_t t = 0; t < app.db->schema->table_count; t++ )
					if ( base::same_folded ( app.db->schema->tables [ t ].name, transfer.table.text.value ( ) ) )
						select_table ( app, static_cast < int32_t > ( t ) );

				set_status ( app, message.value ( ), false );
				app.view = VIEW_DATA;
				app.overlay = OVERLAY_NONE;

			} else
				transfer.parsed = false;

		}

	}

}