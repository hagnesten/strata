#include "studio/app/studio.hh"
#include "sql/print/print.hh"

namespace studio {

	static const char* const type_names [ DESIGN_TYPES ] = { "INTEGER", "TEXT", "REAL", "NUMERIC", "BLOB", "ANY" };

	static uint8_t type_of ( const char* type ) {

		for ( uint8_t i = 0; i < DESIGN_TYPES - 1; i++ )
			if ( type && base::same_folded ( type, type_names [ i ] ) )
				return i;

		if ( !type || !*type )
			return DESIGN_TYPES - 1;

		switch ( sql::affinity_of ( type ) ) {

			case sql::AFFINITY_INTEGER: return 0;
			case sql::AFFINITY_TEXT: return 1;
			case sql::AFFINITY_REAL: return 2;
			case sql::AFFINITY_NUMERIC: return 3;
			case sql::AFFINITY_BLOB: return 4;
			default: return DESIGN_TYPES - 1;

		}

	}

	static void add_column ( design_state& design, const char* name, uint8_t type, bool primary ) {

		if ( design.count == DESIGN_COLUMNS )
			return;

		design_column& column = design.columns [ design.count++ ];

		set_field ( column.name, name );
		set_field ( column.fallback, "" );
		column.type = type;
		column.primary = primary;
		column.not_null = false;
		column.unique = false;
		column.original = -1;

	}

	void open_designer ( studio_state& app, int32_t table ) {

		design_state& design = app.design;

		if ( !app.db )
			return;

		design.count = 0;
		design.table = table;
		design.scroll = 0;
		design.problem.clear ( );

		if ( table < 0 || table >= static_cast < int32_t > ( app.db->schema->table_count ) ) {

			design.table = -1;
			set_field ( design.name, "new_table" );
			add_column ( design, "id", 0, true );
			add_column ( design, "name", 1, false );

		} else {

			const engine::table_info& info = app.db->schema->tables [ table ];
			const sql::create_table_statement* definition = info.definition;

			set_field ( design.name, info.name );

			for ( uint32_t c = 0; c < info.column_count && design.count < DESIGN_COLUMNS; c++ ) {

				const sql::column_definition& column = definition->columns [ c ];
				bool primary = column.primary;

				for ( uint32_t k = 0; k < info.primary_count; k++ )
					primary = primary || info.primary [ k ] == c;

				add_column ( design, column.name, type_of ( column.type ), primary );

				design_column& made = design.columns [ design.count - 1 ];

				made.not_null = column.not_null && !primary;
				made.unique = column.unique;
				made.original = static_cast < int32_t > ( c );

				if ( column.fallback_text )
					set_field ( made.fallback, column.fallback_text );

			}

		}

		app.overlay = OVERLAY_DESIGN;

	}

	static bool simple_value ( const char* text ) {

		if ( !*text )
			return true;

		if ( *text == '\'' || base::same_folded ( text, "null" ) || base::starts_with ( text, "CURRENT_" ) || base::starts_with ( text, "current_" ) )
			return true;

		size_t at = *text == '-' || *text == '+' ? 1 : 0;
		bool digits = false;

		while ( ( text [ at ] >= '0' && text [ at ] <= '9' ) || text [ at ] == '.' ) {

			at++;
			digits = true;

		}

		return digits && !text [ at ];

	}

	static void column_sql ( base::text& out, const design_column& column, bool single_key, bool adding ) {

		sql::print_name ( out, column.name.text.value ( ) );

		if ( column.type != DESIGN_TYPES - 1 )
			out.add ( ' ' ).add ( type_names [ column.type ] );

		if ( column.primary && single_key && !adding )
			out.add ( " PRIMARY KEY" );

		if ( column.not_null && !column.primary )
			out.add ( " NOT NULL" );

		if ( column.unique && !column.primary && !adding )
			out.add ( " UNIQUE" );

		if ( column.fallback.text.size ) {

			out.add ( " DEFAULT " );

			if ( simple_value ( column.fallback.text.value ( ) ) )
				out.add ( column.fallback.text.value ( ) );
			else
				out.add ( '(' ).add ( column.fallback.text.value ( ) ).add ( ')' );

		}

	}

	static void create_sql ( const design_state& design, const char* name, base::text& out ) {

		uint32_t keys = 0;

		for ( uint32_t c = 0; c < design.count; c++ )
			keys += design.columns [ c ].primary;

		out.add ( "CREATE TABLE " );
		sql::print_name ( out, name );
		out.add ( " (" );

		for ( uint32_t c = 0; c < design.count; c++ ) {

			out.add ( c ? ",\n    " : "\n    " );
			column_sql ( out, design.columns [ c ], keys == 1, false );

		}

		if ( keys > 1 ) {

			out.add ( ",\n    PRIMARY KEY (" );

			bool first = true;

			for ( uint32_t c = 0; c < design.count; c++ ) {

				if ( !design.columns [ c ].primary )
					continue;

				out.add ( first ? " " : ", " );
				sql::print_name ( out, design.columns [ c ].name.text.value ( ) );
				first = false;

			}

			out.add ( " )" );

		}

		out.add ( "\n);" );

	}

	static bool needs_rebuild ( const studio_state& app, const design_state& design ) {

		const engine::table_info& info = app.db->schema->tables [ design.table ];
		const sql::create_table_statement* definition = info.definition;

		for ( uint32_t c = 0; c < design.count; c++ ) {

			const design_column& column = design.columns [ c ];

			if ( column.original < 0 ) {

				if ( column.primary || column.unique || ( column.not_null && !column.fallback.text.size ) )
					return true;

				continue;

			}

			const sql::column_definition& before = definition->columns [ column.original ];
			bool primary = before.primary;

			for ( uint32_t k = 0; k < info.primary_count; k++ )
				primary = primary || info.primary [ k ] == static_cast < uint32_t > ( column.original );

			const char* fallback = before.fallback_text ? before.fallback_text : "";

			if ( column.type != type_of ( before.type ) || column.primary != primary || column.not_null != ( before.not_null && !primary ) || column.unique != before.unique || !base::same ( fallback, column.fallback.text.value ( ) ) )
				return true;

		}

		for ( uint32_t c = 1; c < design.count; c++ )
			if ( design.columns [ c ].original >= 0 && design.columns [ c - 1 ].original > design.columns [ c ].original )
				return true;

		return false;

	}

	static void alter_sql ( const studio_state& app, design_state& design, base::text& out ) {

		const engine::table_info& info = app.db->schema->tables [ design.table ];
		const char* name = design.name.text.value ( );
		bool renamed = !base::same ( name, info.name );

		design.rebuild = needs_rebuild ( app, design );

		if ( design.rebuild ) {

			base::text temporary;

			temporary.add ( name ).add ( "_rebuilt" );
			create_sql ( design, temporary.value ( ), out );
			out.add ( "\nINSERT INTO " );
			sql::print_name ( out, temporary.value ( ) );
			out.add ( " (" );

			bool first = true;

			for ( uint32_t c = 0; c < design.count; c++ ) {

				if ( design.columns [ c ].original < 0 )
					continue;

				out.add ( first ? " " : ", " );
				sql::print_name ( out, design.columns [ c ].name.text.value ( ) );
				first = false;

			}

			out.add ( " )\n    SELECT" );
			first = true;

			for ( uint32_t c = 0; c < design.count; c++ ) {

				if ( design.columns [ c ].original < 0 )
					continue;

				out.add ( first ? " " : ", " );
				sql::print_name ( out, info.columns [ design.columns [ c ].original ].name );
				first = false;

			}

			out.add ( " FROM " );
			sql::print_name ( out, info.name );
			out.add ( ";\nDROP TABLE " );
			sql::print_name ( out, info.name );
			out.add ( ";\nALTER TABLE " );
			sql::print_name ( out, temporary.value ( ) );
			out.add ( " RENAME TO " );
			sql::print_name ( out, name );
			out.add ( ';' );

			const engine::catalog& schema = *app.db->schema;

			for ( uint32_t i = 0; i < schema.index_count; i++ ) {

				const engine::index_info& index = schema.indexes [ i ];
				bool kept = !index.automatic && base::same_folded ( index.table, info.name );
				base::text columns;

				for ( uint32_t k = 0; k < index.column_count && kept; k++ ) {

					int32_t found = -1;

					for ( uint32_t c = 0; c < design.count && found < 0; c++ )
						if ( design.columns [ c ].original == static_cast < int32_t > ( index.columns [ k ] ) )
							found = static_cast < int32_t > ( c );

					kept = found >= 0;

					if ( kept ) {

						columns.add ( k ? ", " : " " );
						sql::print_name ( columns, design.columns [ found ].name.text.value ( ) );

					}

				}

				if ( !kept )
					continue;

				out.add ( index.unique ? "\nCREATE UNIQUE INDEX " : index.fulltext ? "\nCREATE FULLTEXT INDEX " : "\nCREATE INDEX " );
				sql::print_name ( out, index.name );
				out.add ( " ON " );
				sql::print_name ( out, name );
				out.add ( " (" ).add ( columns.value ( ) ).add ( " );" );

			}

			return;

		}

		const char* current = info.name;

		if ( renamed ) {

			out.add ( "ALTER TABLE " );
			sql::print_name ( out, info.name );
			out.add ( " RENAME TO " );
			sql::print_name ( out, name );
			out.add ( ";\n" );
			current = name;

		}

		for ( uint32_t c = 0; c < info.column_count; c++ ) {

			bool kept = false;

			for ( uint32_t d = 0; d < design.count && !kept; d++ )
				kept = design.columns [ d ].original == static_cast < int32_t > ( c );

			if ( kept )
				continue;

			out.add ( "ALTER TABLE " );
			sql::print_name ( out, current );
			out.add ( " DROP COLUMN " );
			sql::print_name ( out, info.columns [ c ].name );
			out.add ( ";\n" );

		}

		for ( uint32_t d = 0; d < design.count; d++ ) {

			const design_column& column = design.columns [ d ];

			if ( column.original >= 0 && !base::same ( info.columns [ column.original ].name, column.name.text.value ( ) ) ) {

				out.add ( "ALTER TABLE " );
				sql::print_name ( out, current );
				out.add ( " RENAME COLUMN " );
				sql::print_name ( out, info.columns [ column.original ].name );
				out.add ( " TO " );
				sql::print_name ( out, column.name.text.value ( ) );
				out.add ( ";\n" );

			}

		}

		for ( uint32_t d = 0; d < design.count; d++ ) {

			const design_column& column = design.columns [ d ];

			if ( column.original >= 0 )
				continue;

			out.add ( "ALTER TABLE " );
			sql::print_name ( out, current );
			out.add ( " ADD COLUMN " );
			column_sql ( out, column, false, true );
			out.add ( ";\n" );

		}

		while ( out.size && out.data [ out.size - 1 ] == '\n' )
			out.data [ --out.size ] = 0;

	}

	static bool toggle ( ui_context& ui, uint64_t id, float x, float y, bool& value, bool enabled ) {

		ui::rect box = { x, y, 18, 18 };
		bool clicked = enabled && button ( ui, id, { x - 6, y - 6, 30, 30 }, "" );

		if ( clicked )
			value = !value;

		ui::fill ( *ui.draw, { x - 6, y - 6, 30, 30 }, ui::rgb ( THEME_PANEL ) );
		ui::fill_round ( *ui.draw, box, 3, ui::rgb ( value ? THEME_ACCENT_DEEP : THEME_BACKGROUND ) );
		ui::outline_round ( *ui.draw, box, 3, ui::rgb ( value ? THEME_ACCENT : enabled ? THEME_BORDER : THEME_DIVIDER ) );

		if ( value ) {

			ui::line ( *ui.draw, x + 4.5f, y + 9, x + 7.5f, y + 12.5f, ui::rgb ( 0xF2F6F6 ), 1.6f );
			ui::line ( *ui.draw, x + 7.5f, y + 12.5f, x + 13.5f, y + 5.5f, ui::rgb ( 0xF2F6F6 ), 1.6f );

		}

		return clicked;

	}

	static void apply_design ( studio_state& app ) {

		design_state& design = app.design;
		bool began = !app.db->in_transaction;
		base::text name;

		name.add ( design.name.text.value ( ) );
		design.problem.clear ( );

		if ( began && !engine::execute ( *app.db, "BEGIN", nullptr, 0, nullptr ) ) {

			design.problem.add ( app.db->problem.value ( ) );

			return;

		}

		if ( !engine::execute ( *app.db, design.statements.value ( ), nullptr, 0, nullptr ) ) {

			design.problem.add ( app.db->problem.value ( ) );

			if ( began )
				engine::execute ( *app.db, "ROLLBACK", nullptr, 0, nullptr );

			after_change ( app );

			return;

		}

		if ( began && !engine::execute ( *app.db, "COMMIT", nullptr, 0, nullptr ) ) {

			design.problem.add ( app.db->problem.value ( ) );

			return;

		}

		refresh_schema ( app );

		for ( uint32_t t = 0; t < app.db->schema->table_count; t++ )
			if ( base::same_folded ( app.db->schema->tables [ t ].name, name.value ( ) ) )
				select_table ( app, static_cast < int32_t > ( t ) );

		set_status ( app, design.table < 0 ? "Made the table" : "Changed the table", false );
		app.overlay = OVERLAY_NONE;
		app.view = VIEW_DATA;
		engine::forget_report ( app.check );

	}

	void draw_designer_overlay ( studio_state& app, const ui::rect& area ) {

		ui_context& ui = app.ui;
		ui::renderer& draw = app.draw;
		design_state& design = app.design;
		bool creating = design.table < 0;

		if ( !app.db || ( !creating && design.table >= static_cast < int32_t > ( app.db->schema->table_count ) ) ) {

			app.overlay = OVERLAY_NONE;

			return;

		}

		ui::rect inner = draw_overlay_frame ( app, area, 1080, 720, creating ? "New table" : "Design table" );
		float y = inner.y;

		ui::text ( draw, ui::FONT_UI, "Table name", { inner.x, y, 120, LAYOUT_BUTTON }, ui::rgb ( THEME_DIM ) );
		text_box ( ui, design.name, { inner.x + 120, y, 340, LAYOUT_BUTTON }, "name" );
		y += LAYOUT_BUTTON + 20;

		float name_w = 230;
		float type_w = 130;
		float flag_w = 84;
		float x_type = inner.x + name_w + 12;
		float x_key = x_type + type_w + 12;
		float x_null = x_key + flag_w;
		float x_unique = x_null + flag_w;
		float x_default = x_unique + flag_w;
		float default_w = inner.x + inner.w - 44 - x_default;

		ui::text ( draw, ui::FONT_CAPTION, "COLUMN", { inner.x, y, name_w, 20 }, ui::rgb ( THEME_DIM ) );
		ui::text ( draw, ui::FONT_CAPTION, "TYPE", { x_type, y, type_w, 20 }, ui::rgb ( THEME_DIM ) );
		ui::text ( draw, ui::FONT_CAPTION, "KEY", { x_key, y, flag_w, 20 }, ui::rgb ( THEME_DIM ) );
		ui::text ( draw, ui::FONT_CAPTION, "NOT NULL", { x_null, y, flag_w, 20 }, ui::rgb ( THEME_DIM ) );
		ui::text ( draw, ui::FONT_CAPTION, "UNIQUE", { x_unique, y, flag_w, 20 }, ui::rgb ( THEME_DIM ) );
		ui::text ( draw, ui::FONT_CAPTION, "DEFAULT", { x_default, y, default_w, 20 }, ui::rgb ( THEME_DIM ) );
		y += 26;

		float preview_h = 170;
		ui::rect rows = { inner.x - 8, y, inner.w + 16, inner.y + inner.h - y - preview_h - LAYOUT_BUTTON - 40 };
		int32_t removing = -1;

		if ( ui::inside ( rows, app.input.x, app.input.y ) && app.input.wheel != 0 )
			design.scroll -= app.input.wheel * DESIGN_ROW * 2;

		design.scroll = clamp ( design.scroll, 0, static_cast < float > ( design.count + 1 ) * DESIGN_ROW - rows.h );
		ui::push_clip ( draw, rows );

		for ( uint32_t c = 0; c < design.count; c++ ) {

			design_column& column = design.columns [ c ];
			float ry = rows.y + static_cast < float > ( c ) * DESIGN_ROW - design.scroll;

			if ( ry + DESIGN_ROW < rows.y || ry > rows.y + rows.h )
				continue;

			ui::line ( draw, inner.x, ry + DESIGN_ROW - 0.5f, inner.x + inner.w, ry + DESIGN_ROW - 0.5f, ui::rgb ( THEME_DIVIDER ) );
			text_box ( ui, column.name, { inner.x, ry + 3, name_w, DESIGN_ROW - 8 }, "column", true );

			if ( button ( ui, make_id ( "design.type", c ), { x_type, ry + 3, type_w, DESIGN_ROW - 8 }, type_names [ column.type ] ) )
				column.type = static_cast < uint8_t > ( ( column.type + 1 ) % DESIGN_TYPES );

			toggle ( ui, make_id ( "design.key", c ), x_key + 4, ry + 11, column.primary, true );
			toggle ( ui, make_id ( "design.null", c ), x_null + 4, ry + 11, column.not_null, !column.primary );
			toggle ( ui, make_id ( "design.unique", c ), x_unique + 4, ry + 11, column.unique, !column.primary );
			text_box ( ui, column.fallback, { x_default, ry + 3, default_w, DESIGN_ROW - 8 }, "none", true );

			if ( icon_only ( ui, make_id ( "design.remove", c ), { inner.x + inner.w - 34, ry + 4, 30, 30 }, ICON_TRASH, "Remove this column", design.count > 1 ) )
				removing = static_cast < int32_t > ( c );

		}

		float add_y = rows.y + static_cast < float > ( design.count ) * DESIGN_ROW - design.scroll + 6;
		float add_w = icon_button_width ( draw, "Add column" );

		if ( icon_button ( ui, make_id ( "design.add" ), { inner.x, add_y, add_w, 30 }, ICON_PLUS, "Add column", false, design.count < DESIGN_COLUMNS ) ) {

			base::text name;

			name.add ( "column" ).number ( design.count + 1 );
			add_column ( design, name.value ( ), 1, false );
			focus_field ( ui, design.columns [ design.count - 1 ].name );

		}

		ui::pop_clip ( draw );

		if ( removing >= 0 ) {

			design_column held = { };

			base::swap_bytes ( &held, &design.columns [ removing ], sizeof ( design_column ) );

			for ( uint32_t c = static_cast < uint32_t > ( removing ); c + 1 < design.count; c++ )
				base::copy ( &design.columns [ c ], &design.columns [ c + 1 ], sizeof ( design_column ) );

			design.count--;
			base::copy ( &design.columns [ design.count ], &held, sizeof ( design_column ) );
			base::fill ( &held, 0, sizeof ( held ) );
			design.columns [ design.count ].name.text.clear ( );
			design.columns [ design.count ].fallback.text.clear ( );

		}

		design.statements.clear ( );
		design.rebuild = false;

		if ( creating )
			create_sql ( design, design.name.text.value ( ), design.statements );
		else
			alter_sql ( app, design, design.statements );

		float py = inner.y + inner.h - preview_h - LAYOUT_BUTTON - 24;
		ui::rect preview = { inner.x, py + 26, inner.w, preview_h - 26 };

		caption ( ui, { inner.x, py, inner.w, 22 }, "SQL that will run", design.rebuild ? "the table is rebuilt to change types and constraints" : nullptr );
		ui::fill_round ( draw, preview, LAYOUT_RADIUS, ui::rgb ( THEME_BACKGROUND ) );
		ui::outline_round ( draw, preview, LAYOUT_RADIUS, ui::rgb ( THEME_DIVIDER ) );
		ui::push_clip ( draw, inset ( preview, 1 ) );

		float line_y = preview.y + 8;
		size_t start = 0;
		const char* statements = design.statements.value ( );

		for ( size_t i = 0; i <= design.statements.size; i++ ) {

			if ( i < design.statements.size && statements [ i ] != '\n' )
				continue;

			ui::text ( draw, ui::FONT_CODE_SMALL, statements + start, i - start, { preview.x + 14, line_y, preview.w - 28, 19 }, ui::rgb ( THEME_TEXT ) );
			line_y += 19;
			start = i + 1;

		}

		if ( !design.statements.size )
			ui::text ( draw, ui::FONT_SMALL, "Nothing has changed yet", inset ( preview, 14 ), ui::rgb ( THEME_FAINT ) );

		ui::pop_clip ( draw );

		float go_w = icon_button_width ( draw, creating ? "Create table" : "Apply" );
		ui::rect go = { inner.x + inner.w - go_w, inner.y + inner.h - LAYOUT_BUTTON, go_w, LAYOUT_BUTTON };

		if ( design.problem.size )
			ui::text ( draw, ui::FONT_SMALL, design.problem.value ( ), design.problem.size, { inner.x, go.y, inner.w - go_w - 20, LAYOUT_BUTTON }, ui::rgb ( THEME_ERROR ) );

		if ( icon_button ( ui, make_id ( "design.apply" ), go, creating ? ICON_PLUS : ICON_CHECK, creating ? "Create table" : "Apply", true, design.statements.size > 0 && design.name.text.size > 0 ) )
			apply_design ( app );

	}

}