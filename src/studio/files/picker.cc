#include "studio/app/studio.hh"

namespace studio {

	static bool database_name ( const char* name ) {

		size_t size = base::length ( name );
		static const char* const endings [ 3 ] = { ".sdb", ".db", ".strata" };

		for ( const char* ending : endings ) {

			size_t length = base::length ( ending );

			if ( size > length && base::same_folded ( name + size - length, ending ) )
				return true;

		}

		return false;

	}

	static void list_picker ( studio_state& app ) {

		picker_state& picker = app.picker;

		picker.scroll = 0;
		picker.problem.clear ( );
		picker.drives = !picker.folder.text.size;
		picker.listed = picker.drives ? base::list_drives ( picker.entries ) : base::list_folder ( picker.folder.text.value ( ), picker.entries );

		if ( !picker.listed )
			picker.problem.add ( "That folder could not be read" );

	}

	static void go_to ( studio_state& app, const char* folder ) {

		set_field ( app.picker.folder, folder );
		list_picker ( app );

	}

	void open_picker ( studio_state& app ) {

		picker_state& picker = app.picker;
		base::text folder;

		if ( !picker.folder.text.size ) {

			if ( app.path [ 0 ] )
				parent_folder ( app.path, folder );
			else
				documents_folder ( folder );

			set_field ( picker.folder, folder.value ( ) );

		}

		set_field ( picker.name, "" );
		list_picker ( app );
		app.overlay = OVERLAY_OPEN;

	}

	static void open_from_picker ( studio_state& app, const char* path ) {

		if ( open_database_file ( app, path ) ) {

			app.overlay = OVERLAY_NONE;
			app.view = VIEW_DATA;

		} else {

			app.picker.problem.clear ( );
			app.picker.problem.add ( app.status.value ( ) );

		}

	}

	static void create_database ( studio_state& app ) {

		picker_state& picker = app.picker;

		if ( !picker.name.text.size || !picker.folder.text.size )
			return;

		base::text path;

		path.add ( picker.folder.text.value ( ) );

		if ( path.data [ path.size - 1 ] != '\\' )
			path.add ( '\\' );

		path.add ( picker.name.text.value ( ) );

		if ( !database_name ( picker.name.text.value ( ) ) )
			path.add ( ".sdb" );

		if ( base::path_exists ( path.value ( ) ) ) {

			picker.problem.clear ( );
			picker.problem.add ( "A file with that name is already there" );

			return;

		}

		open_from_picker ( app, path.value ( ) );

	}

	static void place_row ( studio_state& app, const ui::rect& row, icon_kind icon, const char* title, const char* detail, uint64_t id, bool& clicked ) {

		ui_context& ui = app.ui;
		ui::renderer& draw = app.draw;

		clicked = list_row ( ui, id, row, false );
		draw_icon ( draw, icon, row.x + 10, row.y + ( detail ? 10 : ( row.h - ICON_SIZE ) * 0.5f ), ui::rgb ( icon == ICON_FILE ? THEME_ACCENT : THEME_DIM ) );

		if ( detail ) {

			ui::text ( draw, ui::FONT_UI, title, { row.x + 36, row.y + 4, row.w - 44, 22 }, ui::rgb ( THEME_TEXT ) );
			ui::text ( draw, ui::FONT_SMALL, detail, { row.x + 36, row.y + 24, row.w - 44, 18 }, ui::rgb ( THEME_FAINT ) );

		} else
			ui::text ( draw, ui::FONT_UI, title, { row.x + 36, row.y, row.w - 44, row.h }, ui::rgb ( THEME_TEXT ) );

	}

	void draw_open_overlay ( studio_state& app, const ui::rect& area ) {

		ui_context& ui = app.ui;
		ui::renderer& draw = app.draw;
		picker_state& picker = app.picker;
		ui::rect inner = draw_overlay_frame ( app, area, 940, 620, "Open a database" );
		float left_w = 300;
		float y = inner.y;

		caption ( ui, { inner.x, y, left_w - 20, LAYOUT_CAPTION }, "Recent" );
		y += LAYOUT_CAPTION;

		if ( !app.recent.count )
			ui::text ( draw, ui::FONT_SMALL, "Files you open show up here", { inner.x, y, left_w - 20, 26 }, ui::rgb ( THEME_FAINT ) );

		for ( uint32_t i = 0; i < app.recent.count && y < inner.y + inner.h - 160; i++ ) {

			base::text name;
			base::text folder;
			bool clicked = false;

			file_name_of ( app.recent.paths [ i ], name );
			parent_folder ( app.recent.paths [ i ], folder );
			place_row ( app, { inner.x - 8, y, left_w - 12, 46 }, ICON_FILE, name.value ( ), folder.value ( ), make_id ( "recent", i ), clicked );

			if ( clicked ) {

				open_from_picker ( app, app.recent.paths [ i ] );

				return;

			}

			y += 48;

		}

		y = base::max ( y + 14, inner.y + inner.h - 150 );
		caption ( ui, { inner.x, y, left_w - 20, LAYOUT_CAPTION }, "Places" );
		y += LAYOUT_CAPTION;

		base::text documents;
		base::text desktop;
		bool clicked = false;

		documents_folder ( documents );
		base::environment_path ( L"USERPROFILE", desktop );
		desktop.add ( "\\Desktop" );
		place_row ( app, { inner.x - 8, y, left_w - 12, 34 }, ICON_OPEN, "Strata documents", nullptr, make_id ( "place.documents" ), clicked );

		if ( clicked )
			go_to ( app, documents.value ( ) );

		place_row ( app, { inner.x - 8, y + 36, left_w - 12, 34 }, ICON_OPEN, "Desktop", nullptr, make_id ( "place.desktop" ), clicked );

		if ( clicked )
			go_to ( app, desktop.value ( ) );

		place_row ( app, { inner.x - 8, y + 72, left_w - 12, 34 }, ICON_DRIVE, "This PC", nullptr, make_id ( "place.drives" ), clicked );

		if ( clicked )
			go_to ( app, "" );

		float rx = inner.x + left_w;
		float rw = inner.w - left_w;

		ui::line ( draw, rx - 18.5f, inner.y, rx - 18.5f, inner.y + inner.h, ui::rgb ( THEME_DIVIDER ) );

		if ( icon_only ( ui, make_id ( "picker.up" ), { rx, inner.y, LAYOUT_BUTTON, LAYOUT_BUTTON }, ICON_UP, "Go to the folder above", picker.folder.text.size > 0 ) ) {

			base::text up;

			parent_folder ( picker.folder.text.value ( ), up );
			go_to ( app, up.size == picker.folder.text.size ? "" : up.value ( ) );

		}

		if ( text_box ( ui, picker.folder, { rx + LAYOUT_BUTTON + 8, inner.y, rw - LAYOUT_BUTTON - 8, LAYOUT_BUTTON }, "This PC" ) == FIELD_SUBMIT )
			list_picker ( app );

		ui::rect list = { rx, inner.y + LAYOUT_BUTTON + 12, rw, inner.h - LAYOUT_BUTTON * 2 - 40 };
		uint32_t shown = 0;

		ui::fill_round ( draw, list, LAYOUT_RADIUS, ui::rgb ( THEME_BACKGROUND ) );
		ui::outline_round ( draw, list, LAYOUT_RADIUS, ui::rgb ( THEME_DIVIDER ) );

		if ( ui::inside ( list, app.input.x, app.input.y ) && app.input.wheel != 0 )
			picker.scroll -= app.input.wheel * 34 * 3;

		ui::push_clip ( draw, inset ( list, 1 ) );

		float ly = list.y + 4 - picker.scroll;
		const char* chosen = nullptr;
		bool chosen_folder = false;

		for ( uint32_t i = 0; i < picker.entries.count; i++ ) {

			const base::folder_entry& entry = picker.entries.items [ i ];

			if ( !entry.directory && !database_name ( entry.name ) )
				continue;

			shown++;

			if ( ly + 34 >= list.y && ly <= list.y + list.h ) {

				bool hit = false;
				base::text size;

				if ( !entry.directory )
					size_text ( size, entry.size );

				place_row ( app, { list.x + 4, ly, list.w - 8 - LAYOUT_SCROLLBAR, 34 }, picker.drives ? ICON_DRIVE : entry.directory ? ICON_OPEN : ICON_FILE, entry.name, nullptr, make_id ( "entry", i ), hit );

				if ( size.size )
					ui::text ( draw, ui::FONT_SMALL, size.value ( ), size.size, { list.x, ly, list.w - 24, 34 }, ui::rgb ( THEME_FAINT ), ui::ALIGN_RIGHT );

				if ( hit && ui::inside ( list, app.input.x, app.input.y ) ) {

					chosen = entry.name;
					chosen_folder = entry.directory;

				}

			}

			ly += 34;

		}

		ui::pop_clip ( draw );

		float content = static_cast < float > ( shown ) * 34 + 8;

		picker.scroll = clamp ( picker.scroll, 0, content - list.h );
		picker.scroll = scrollbar ( ui, make_id ( "picker.scroll" ), { list.x + list.w - LAYOUT_SCROLLBAR - 1, list.y + 1, LAYOUT_SCROLLBAR, list.h - 2 }, content, list.h, picker.scroll, true );

		if ( !shown )
			ui::text ( draw, ui::FONT_UI, picker.problem.size ? picker.problem.value ( ) : "No databases or folders here", list, ui::rgb ( THEME_FAINT ), ui::ALIGN_CENTER );

		float by = list.y + list.h + 12;
		float create_w = icon_button_width ( draw, "Create" );

		ui::text ( draw, ui::FONT_UI, "New database", { rx, by, 120, LAYOUT_BUTTON }, ui::rgb ( THEME_DIM ) );

		if ( text_box ( ui, picker.name, { rx + 120, by, rw - 120 - create_w - 10, LAYOUT_BUTTON }, "name.sdb" ) == FIELD_SUBMIT )
			create_database ( app );

		if ( icon_button ( ui, make_id ( "picker.create" ), { rx + rw - create_w, by, create_w, LAYOUT_BUTTON }, ICON_PLUS, "Create", true, picker.name.text.size > 0 && !picker.drives ) )
			create_database ( app );

		if ( picker.problem.size && shown )
			ui::text ( draw, ui::FONT_SMALL, picker.problem.value ( ), picker.problem.size, { rx, inner.y + inner.h - 4, rw, 18 }, ui::rgb ( THEME_ERROR ) );

		if ( !chosen )
			return;

		base::text path;

		path.add ( picker.folder.text.value ( ) );

		if ( path.size && path.data [ path.size - 1 ] != '\\' )
			path.add ( '\\' );

		path.add ( chosen );

		if ( chosen_folder )
			go_to ( app, path.value ( ) );
		else
			open_from_picker ( app, path.value ( ) );

	}

}