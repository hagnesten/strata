#include "studio/app/studio.hh"

namespace studio {

	static float section ( ui_context& ui, float x, float y, float w, const char* label ) {

		ui::line ( *ui.draw, x, y + 0.5f, x + w, y + 0.5f, ui::rgb ( THEME_DIVIDER ) );
		caption ( ui, { x + 16, y + 6, w - 32, LAYOUT_CAPTION }, label );

		return y + 6 + LAYOUT_CAPTION;

	}

	static float pair ( ui_context& ui, float x, float y, float w, const char* key, const base::text& value, uint32_t shade = THEME_TEXT ) {

		key_value ( ui, { x + 16, y, w - 32, 24 }, key, value.value ( ), ui::rgb ( shade ) );

		return y + 24;

	}

	static void draw_inspector ( studio_state& app, const ui::rect& area, engine::table_info& table, const table_stats& stats ) {

		ui_context& ui = app.ui;
		ui::renderer& draw = app.draw;
		engine::catalog& schema = *app.db->schema;
		float x = area.x;
		float w = area.w;
		float y = area.y;
		base::text value;

		ui::fill ( draw, area, ui::rgb ( THEME_PANEL ) );
		ui::line ( draw, area.x + 0.5f, area.y, area.x + 0.5f, area.y + area.h, ui::rgb ( THEME_DIVIDER ) );
		ui::push_clip ( draw, area );
		caption ( ui, { x + 16, y + 6, w - 32, LAYOUT_CAPTION }, "Table" );
		y += 6 + LAYOUT_CAPTION;
		grouped ( value, stats.rows );
		y = pair ( ui, x, y, w, "Rows", value );
		value.clear ( );
		value.number ( table.column_count );
		y = pair ( ui, x, y, w, "Columns", value );
		value.clear ( );

		if ( table.rowid_column >= 0 )
			value.add ( table.columns [ table.rowid_column ].name ).add ( " (row id)" );
		else if ( table.primary_count ) {

			for ( uint32_t k = 0; k < table.primary_count; k++ )
				value.add ( k ? ", " : "" ).add ( table.columns [ table.primary [ k ] ].name );

		} else
			value.add ( "row id" );

		y = pair ( ui, x, y, w, "Key", value );
		value.clear ( );
		value.number ( table.root );
		y = pair ( ui, x, y, w, "Root page", value, THEME_ACCENT_SOFT );

		y = section ( ui, x, y + 8, w, "Storage" );
		value.clear ( );
		value.number ( stats.pages );
		y = pair ( ui, x, y, w, "Pages", value );
		value.clear ( );
		value.number ( stats.leaves );
		y = pair ( ui, x, y, w, "Leaves", value );
		value.clear ( );
		value.number ( stats.depth + 1 );
		y = pair ( ui, x, y, w, "Levels", value );
		value.clear ( );
		value.number ( stats.overflow );
		y = pair ( ui, x, y, w, "Overflow pages", value );
		value.clear ( );
		size_text ( value, static_cast < uint64_t > ( stats.pages ) * PAGE_SIZE );
		y = pair ( ui, x, y, w, "On disk", value );

		float fill = stats.pages ? static_cast < float > ( stats.used ) / static_cast < float > ( static_cast < uint64_t > ( stats.pages ) * PAGE_USABLE ) : 0;

		value.clear ( );
		value.number ( static_cast < uint64_t > ( fill * 100 + 0.5f ) ).add ( '%' );
		y = pair ( ui, x, y, w, "Page fill", value );

		ui::rect bar = { x + 16, y + 4, w - 32, 4 };

		ui::fill_round ( draw, bar, 2, ui::rgb ( THEME_RAISED ) );
		ui::fill_round ( draw, { bar.x, bar.y, bar.w * clamp ( fill, 0, 1 ), bar.h }, 2, ui::rgb ( THEME_ACCENT ) );
		y += 20;

		y = section ( ui, x, y, w, "Columns" );

		for ( uint32_t c = 0; c < table.column_count; c++ ) {

			const engine::column_info& column = table.columns [ c ];
			base::text flags;

			if ( column.type && *column.type )
				flags.add ( column.type );

			if ( column.rowid )
				flags.add ( flags.size ? " \xC2\xB7 key" : "key" );

			if ( column.not_null && !column.rowid )
				flags.add ( flags.size ? " \xC2\xB7 not null" : "not null" );

			ui::text ( draw, ui::FONT_UI, column.name, { x + 16, y, w * 0.5f - 16, 24 }, ui::rgb ( THEME_TEXT ) );
			ui::text ( draw, ui::FONT_SMALL, flags.value ( ), flags.size, { x + w * 0.5f, y, w * 0.5f - 16, 24 }, ui::rgb ( THEME_DIM ) );
			y += 24;

		}

		bool any_index = false;

		for ( uint32_t i = 0; i < schema.index_count; i++ ) {

			engine::index_info& index = schema.indexes [ i ];

			if ( !base::same_folded ( index.table, table.name ) )
				continue;

			if ( !any_index )
				y = section ( ui, x, y + 8, w, "Indexes" );

			any_index = true;

			base::text columns;

			for ( uint32_t k = 0; k < index.column_count; k++ )
				columns.add ( k ? ", " : "" ).add ( table.columns [ index.columns [ k ] ].name );

			ui::text ( draw, ui::FONT_UI, index.automatic ? ( index.unique ? "unique" : "automatic" ) : index.name, { x + 16, y, w * 0.5f - 16, 24 }, ui::rgb ( index.automatic ? THEME_DIM : THEME_TEXT ) );
			ui::text ( draw, ui::FONT_UI, columns.value ( ), columns.size, { x + w * 0.5f, y, w * 0.5f - 16, 24 }, ui::rgb ( THEME_DIM ) );
			y += 24;

		}

		if ( table.foreign_count ) {

			y = section ( ui, x, y + 8, w, "References" );

			for ( uint32_t f = 0; f < table.foreign_count; f++ ) {

				const engine::foreign_key& key = table.foreign [ f ];
				base::text left;
				base::text right;

				for ( uint32_t k = 0; k < key.column_count; k++ )
					left.add ( k ? ", " : "" ).add ( table.columns [ key.columns [ k ] ].name );

				right.add ( key.parent );

				if ( key.on_delete == sql::FOREIGN_CASCADE )
					right.add ( " \xC2\xB7 cascade" );
				else if ( key.on_delete == sql::FOREIGN_SET_NULL )
					right.add ( " \xC2\xB7 set null" );

				ui::text ( draw, ui::FONT_UI, left.value ( ), left.size, { x + 16, y, w * 0.5f - 28, 24 }, ui::rgb ( THEME_TEXT ) );
				ui::text ( draw, ui::FONT_SMALL, "\xE2\x86\x92", { x + w * 0.5f - 16, y, 16, 24 }, ui::rgb ( THEME_FUNCTION ) );
				ui::text ( draw, ui::FONT_SMALL, right.value ( ), right.size, { x + w * 0.5f, y, w * 0.5f - 16, 24 }, ui::rgb ( THEME_DIM ) );
				y += 24;

			}

		}

		ui::pop_clip ( draw );

	}

	void draw_data_view ( studio_state& app, const ui::rect& area ) {

		ui::renderer& draw = app.draw;

		if ( !app.db || !app.db->open || app.table < 0 ) {

			ui::text ( draw, ui::FONT_UI, app.db && app.db->open ? "This database has no tables yet. Make one in the Query tab." : "No database is open. Drop a file on the window.", area, ui::rgb ( THEME_FAINT ), ui::ALIGN_CENTER );

			return;

		}

		engine::table_info& table = app.db->schema->tables [ app.table ];

		ensure_stats ( app, app.table );

		const table_stats& stats = app.stats [ app.table ];
		float inspector = LAYOUT_INSPECTOR;
		ui::rect head = { area.x, area.y, area.w - inspector, 54 };

		ui::fill ( draw, head, ui::rgb ( THEME_BACKGROUND ) );
		table_glyph ( draw, head.x + 18, head.y + 13, ui::rgb ( THEME_ACCENT ) );
		ui::text ( draw, ui::FONT_TITLE, table.name, { head.x + 40, head.y + 4, head.w - 60, 26 }, ui::rgb ( THEME_TEXT ) );

		base::text line;

		grouped ( line, stats.rows );
		line.add ( stats.rows == 1 ? " row   " : " rows   " ).number ( table.column_count ).add ( table.column_count == 1 ? " column   " : " columns   " ).number ( stats.pages ).add ( stats.pages == 1 ? " page" : " pages" );

		if ( app.data_grid.rows < stats.rows )
			line.add ( "   showing the first " ).number ( app.data_grid.rows );

		ui::text ( draw, ui::FONT_SMALL, line.value ( ), line.size, { head.x + 40, head.y + 28, head.w - 60, 18 }, ui::rgb ( THEME_DIM ) );
		ui::line ( draw, head.x, head.y + head.h - 0.5f, head.x + head.w, head.y + head.h - 0.5f, ui::rgb ( THEME_DIVIDER ) );
		draw_grid ( app.ui, app.data_grid, { area.x, area.y + head.h, area.w - inspector, area.h - head.h }, stats.rows ? "Loading rows" : "This table is empty" );
		draw_inspector ( app, { area.x + area.w - inspector, area.y, inspector, area.h }, table, stats );

	}

}
