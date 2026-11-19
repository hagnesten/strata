#include "studio/app/studio.hh"

namespace studio {

	static bool read_entry ( const base::text& body, size_t& at, const char*& out, size_t& size ) {

		size_t length = 0;
		bool digits = false;

		while ( at < body.size && body.data [ at ] >= '0' && body.data [ at ] <= '9' ) {

			length = length * 10 + static_cast < size_t > ( body.data [ at++ ] - '0' );
			digits = true;

		}

		if ( !digits || at >= body.size || body.data [ at ] != '\n' || at + 1 + length > body.size )
			return false;

		out = body.value ( ) + at + 1;
		size = length;
		at += 1 + length;

		if ( at < body.size && body.data [ at ] == '\n' )
			at++;

		return true;

	}

	static void write_entry ( base::text& out, const char* value ) {

		size_t size = base::length ( value );

		out.number ( size ).add ( '\n' ).add ( value, size ).add ( '\n' );

	}

	static void save_history ( studio_state& app ) {

		base::text path;
		base::text body;

		if ( app.capturing || !settings_file ( path, HISTORY_FILE ) )
			return;

		for ( uint32_t i = 0; i < app.library.history_count; i++ )
			write_entry ( body, app.library.history [ i ] );

		base::write_whole ( path.value ( ), body.value ( ), body.size );

	}

	static void save_saved ( studio_state& app ) {

		base::text path;
		base::text body;

		if ( app.capturing || !settings_file ( path, SAVED_FILE ) )
			return;

		for ( uint32_t i = 0; i < app.library.saved_count; i++ ) {

			write_entry ( body, app.library.saved [ i ].name );
			write_entry ( body, app.library.saved [ i ].sql );

		}

		base::write_whole ( path.value ( ), body.value ( ), body.size );

	}

	void load_library ( studio_state& app ) {

		query_library& library = app.library;
		base::text path;
		base::text body;

		library.memory.reset ( );
		library.history = nullptr;
		library.history_count = 0;
		library.history_room = 0;
		library.saved = nullptr;
		library.saved_count = 0;
		library.saved_room = 0;

		if ( settings_file ( path, HISTORY_FILE ) && base::read_whole ( path.value ( ), body ) ) {

			size_t at = 0;
			const char* entry = nullptr;
			size_t size = 0;

			while ( library.history_count < STUDIO_HISTORY && read_entry ( body, at, entry, size ) ) {

				const char* kept = library.memory.keep ( entry, size );

				if ( kept )
					base::push ( library.memory, library.history, library.history_count, library.history_room, kept );

			}

		}

		if ( settings_file ( path, SAVED_FILE ) && base::read_whole ( path.value ( ), body ) ) {

			size_t at = 0;
			const char* name = nullptr;
			const char* sql = nullptr;
			size_t name_size = 0;
			size_t sql_size = 0;

			while ( library.saved_count < STUDIO_SAVED && read_entry ( body, at, name, name_size ) && read_entry ( body, at, sql, sql_size ) ) {

				saved_query entry = { library.memory.keep ( name, name_size ), library.memory.keep ( sql, sql_size ) };

				if ( entry.name && entry.sql )
					base::push ( library.memory, library.saved, library.saved_count, library.saved_room, entry );

			}

		}

	}

	void note_history ( studio_state& app, const char* sql, size_t size ) {

		query_library& library = app.library;

		while ( size && ( sql [ size - 1 ] == ' ' || sql [ size - 1 ] == '\n' || sql [ size - 1 ] == ';' ) )
			size--;

		if ( !size )
			return;

		if ( library.history_count && base::length ( library.history [ library.history_count - 1 ] ) == size && base::same_bytes ( library.history [ library.history_count - 1 ], sql, size ) )
			return;

		if ( library.history_count == STUDIO_HISTORY ) {

			base::move ( library.history, library.history + 1, sizeof ( const char* ) * ( STUDIO_HISTORY - 1 ) );
			library.history_count--;

		}

		const char* kept = library.memory.keep ( sql, size );

		if ( kept && base::push ( library.memory, library.history, library.history_count, library.history_room, kept ) )
			save_history ( app );

	}

	void save_query ( studio_state& app, const char* name, const char* sql, size_t size ) {

		query_library& library = app.library;
		const char* kept_sql = library.memory.keep ( sql, size );
		const char* kept_name = library.memory.keep ( name, base::length ( name ) );

		if ( !kept_sql || !kept_name )
			return;

		for ( uint32_t i = 0; i < library.saved_count; i++ ) {

			if ( base::same_folded ( library.saved [ i ].name, name ) ) {

				library.saved [ i ].sql = kept_sql;
				save_saved ( app );

				return;

			}

		}

		if ( library.saved_count == STUDIO_SAVED )
			return;

		saved_query entry = { kept_name, kept_sql };

		if ( base::push ( library.memory, library.saved, library.saved_count, library.saved_room, entry ) )
			save_saved ( app );

	}

	static void load_into_editor ( studio_state& app, const char* sql ) {

		editor_replace ( app.editor, 0, app.editor.text.size, sql, base::length ( sql ) );
		app.ui.focus = app.editor.id;
		app.ui.redraw = true;

	}

	static void first_line ( const char* sql, base::text& out ) {

		size_t at = 0;

		out.clear ( );

		while ( sql [ at ] == ' ' || sql [ at ] == '\n' )
			at++;

		while ( sql [ at ] && sql [ at ] != '\n' && out.size < 120 )
			out.add ( sql [ at++ ] );

	}

	static uint32_t line_count ( const char* sql ) {

		uint32_t lines = 1;

		for ( const char* at = sql; *at; at++ )
			lines += *at == '\n';

		return lines;

	}

	void draw_library ( studio_state& app, const ui::rect& area ) {

		ui_context& ui = app.ui;
		ui::renderer& draw = app.draw;
		query_library& library = app.library;
		float x = area.x + 18;
		float w = area.w - 36;
		float y = area.y + 14;

		ui::fill ( draw, area, ui::rgb ( THEME_PANEL ) );
		ui::line ( draw, area.x + 0.5f, area.y, area.x + 0.5f, area.y + area.h, ui::rgb ( THEME_DIVIDER ) );

		float half = ( w - 40 ) * 0.5f;

		if ( segment ( ui, make_id ( "library.history" ), { x, y, half, 30 }, "History", library.page == LIBRARY_HISTORY, true, false ) ) {

			library.page = LIBRARY_HISTORY;
			library.scroll = 0;

		}

		if ( segment ( ui, make_id ( "library.saved" ), { x + half, y, half, 30 }, "Saved", library.page == LIBRARY_SAVED, false, true ) ) {

			library.page = LIBRARY_SAVED;
			library.scroll = 0;

		}

		if ( icon_only ( ui, make_id ( "library.close" ), { x + w - 30, y, 30, 30 }, ICON_CLOSE, "Hide the panel" ) )
			library.page = LIBRARY_HIDDEN;

		y += 44;

		if ( library.naming ) {

			caption ( ui, { x, y, w, 24 }, "Save this query as" );
			y += 26;

			field_event event = text_box ( ui, library.save_name, { x, y, w, LAYOUT_BUTTON }, "Name" );
			float save_w = icon_button_width ( draw, "Save" );
			bool save = icon_button ( ui, make_id ( "library.save" ), { x + w - save_w, y + LAYOUT_BUTTON + 8, save_w, LAYOUT_BUTTON }, ICON_SAVE, "Save", true, library.save_name.text.size > 0 );

			if ( icon_button ( ui, make_id ( "library.cancel" ), { x + w - save_w - 10 - icon_button_width ( draw, "Cancel" ), y + LAYOUT_BUTTON + 8, icon_button_width ( draw, "Cancel" ), LAYOUT_BUTTON }, ICON_CLOSE, "Cancel" ) || event == FIELD_CANCEL )
				library.naming = false;

			if ( ( save || event == FIELD_SUBMIT ) && library.save_name.text.size ) {

				save_query ( app, library.save_name.text.value ( ), app.editor.text.value ( ), app.editor.text.size );
				library.naming = false;

				base::text message;

				message.add ( "Saved the query as " ).add ( library.save_name.text.value ( ) );
				set_status ( app, message.value ( ), false );

			}

			y += LAYOUT_BUTTON * 2 + 20;
			ui::line ( draw, area.x, y - 8.5f, area.x + area.w, y - 8.5f, ui::rgb ( THEME_DIVIDER ) );

		}

		ui::rect list = { area.x + 1, y, area.w - 1, area.y + area.h - y };
		bool history = library.page == LIBRARY_HISTORY;
		uint32_t count = history ? library.history_count : library.saved_count;
		float row_h = 58;

		if ( ui::inside ( list, app.input.x, app.input.y ) && app.input.wheel != 0 )
			library.scroll -= app.input.wheel * row_h * 2;

		library.scroll = clamp ( library.scroll, 0, static_cast < float > ( count ) * row_h - list.h + 10 );

		if ( !count ) {

			ui::text ( draw, ui::FONT_SMALL, history ? "Queries you run show up here" : "Save a query to keep it here", { list.x + 18, list.y, list.w - 36, 30 }, ui::rgb ( THEME_FAINT ) );

			return;

		}

		ui::push_clip ( draw, list );

		int32_t removing = -1;

		for ( uint32_t n = 0; n < count; n++ ) {

			uint32_t i = history ? count - 1 - n : n;
			float ry = list.y + static_cast < float > ( n ) * row_h - library.scroll;

			if ( ry + row_h < list.y || ry > list.y + list.h )
				continue;

			const char* sql = history ? library.history [ i ] : library.saved [ i ].sql;
			ui::rect row = { list.x + 8, ry, list.w - 16 - LAYOUT_SCROLLBAR, row_h - 4 };
			base::text line;
			base::text detail;

			first_line ( sql, line );

			bool over_delete = !history && ui::inside ( { row.x + row.w - 30, row.y + 4, 26, 26 }, app.input.x, app.input.y );

			if ( list_row ( ui, make_id ( history ? "library.h" : "library.s", i ), row, false ) && ui::inside ( list, app.input.x, app.input.y ) && !over_delete ) {

				load_into_editor ( app, sql );

				base::text message;

				message.add ( "Loaded " ).add ( history ? "a query from the history" : library.saved [ i ].name );
				set_status ( app, message.value ( ), false );

			}

			if ( ui::inside ( row, app.input.x, app.input.y ) )
				hover_text ( ui, "Click to put this query in the editor" );

			if ( history ) {

				ui::text ( draw, ui::FONT_CODE_SMALL, line.value ( ), line.size, { row.x + 12, row.y + 6, row.w - 24, 22 }, ui::rgb ( THEME_TEXT ) );
				detail.number ( line_count ( sql ) ).add ( line_count ( sql ) == 1 ? " line" : " lines" );
				ui::text ( draw, ui::FONT_SMALL, detail.value ( ), detail.size, { row.x + 12, row.y + 28, row.w - 24, 20 }, ui::rgb ( THEME_FAINT ) );

			} else {

				draw_icon ( draw, ICON_SAVE, row.x + 10, row.y + 9, ui::rgb ( THEME_ACCENT ) );
				ui::text ( draw, ui::FONT_STRONG, library.saved [ i ].name, { row.x + 34, row.y + 5, row.w - 70, 22 }, ui::rgb ( THEME_TEXT ) );
				ui::text ( draw, ui::FONT_CODE_SMALL, line.value ( ), line.size, { row.x + 12, row.y + 28, row.w - 24, 20 }, ui::rgb ( THEME_DIM ) );

				if ( ui::inside ( row, app.input.x, app.input.y ) && icon_only ( ui, make_id ( "library.delete", i ), { row.x + row.w - 30, row.y + 4, 26, 26 }, ICON_TRASH, "Remove this saved query" ) )
					removing = static_cast < int32_t > ( i );

			}

			ui::line ( draw, row.x + 8, ry + row_h - 0.5f, row.x + row.w - 8, ry + row_h - 0.5f, ui::rgb ( THEME_DIVIDER ) );

		}

		ui::pop_clip ( draw );
		library.scroll = scrollbar ( ui, make_id ( "library.scroll" ), { area.x + area.w - LAYOUT_SCROLLBAR, list.y, LAYOUT_SCROLLBAR, list.h }, static_cast < float > ( count ) * row_h + 10, list.h, library.scroll, true );

		if ( removing >= 0 ) {

			for ( uint32_t i = static_cast < uint32_t > ( removing ); i + 1 < library.saved_count; i++ )
				library.saved [ i ] = library.saved [ i + 1 ];

			library.saved_count--;
			save_saved ( app );

		}

	}

}