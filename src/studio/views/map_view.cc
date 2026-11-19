#include "studio/app/studio.hh"

namespace studio {

	static float card_height ( const engine::table_info& table ) {

		return 36 + static_cast < float > ( table.column_count ) * STUDIO_CARD_ROW + 8;

	}

	static void place_cards ( studio_state& app ) {

		engine::catalog& schema = *app.db->schema;
		uint32_t levels [ CATALOG_TABLES ] = { };

		for ( uint32_t pass = 0; pass < 8; pass++ ) {

			for ( uint32_t t = 0; t < schema.table_count; t++ ) {

				for ( uint32_t f = 0; f < schema.tables [ t ].foreign_count; f++ ) {

					engine::table_info* parent = engine::find_table ( schema, schema.tables [ t ].foreign [ f ].parent );

					if ( !parent || parent == &schema.tables [ t ] )
						continue;

					uint32_t p = static_cast < uint32_t > ( parent - schema.tables );

					if ( levels [ p ] + 1 > levels [ t ] && levels [ p ] < 6 )
						levels [ t ] = levels [ p ] + 1;

				}

			}

		}

		float order [ CATALOG_TABLES ] = { };
		uint32_t placed [ CATALOG_TABLES ];
		uint32_t placed_count = 0;

		for ( uint32_t level = 0; level < 8; level++ ) {

			uint32_t first = placed_count;

			for ( uint32_t t = 0; t < schema.table_count; t++ ) {

				if ( ( levels [ t ] < 7 ? levels [ t ] : 7 ) != level )
					continue;

				float sum = 0;
				uint32_t parents = 0;

				for ( uint32_t f = 0; f < schema.tables [ t ].foreign_count; f++ ) {

					engine::table_info* parent = engine::find_table ( schema, schema.tables [ t ].foreign [ f ].parent );

					if ( !parent || parent == &schema.tables [ t ] )
						continue;

					sum += order [ parent - schema.tables ];
					parents++;

				}

				order [ t ] = parents ? sum / static_cast < float > ( parents ) : static_cast < float > ( t );
				placed [ placed_count++ ] = t;

			}

			for ( uint32_t i = first + 1; i < placed_count; i++ ) {

				uint32_t here = placed [ i ];
				uint32_t k = i;

				while ( k > first && order [ placed [ k - 1 ] ] > order [ here ] ) {

					placed [ k ] = placed [ k - 1 ];
					k--;

				}

				placed [ k ] = here;

			}

			float y = 40;

			for ( uint32_t i = first; i < placed_count; i++ ) {

				uint32_t t = placed [ i ];

				app.cards [ t ].x = 48 + static_cast < float > ( level ) * ( STUDIO_CARD_WIDTH + 120 );
				app.cards [ t ].y = y;
				app.cards [ t ].placed = true;
				order [ t ] = static_cast < float > ( i - first );
				y += card_height ( schema.tables [ t ] ) + 40;

			}

		}

		app.map_x = 0;
		app.map_y = 0;
		app.cards_ready = true;

	}

	static int32_t parent_row ( const engine::table_info& parent, const engine::foreign_key& key ) {

		if ( key.parent_count ) {

			int32_t column = engine::find_column ( parent, key.parent_columns [ 0 ] );

			return column;

		}

		if ( parent.rowid_column >= 0 )
			return parent.rowid_column;

		return parent.primary_count ? static_cast < int32_t > ( parent.primary [ 0 ] ) : -1;

	}

	static void draw_schema_map ( studio_state& app, const ui::rect& area ) {

		ui_context& ui = app.ui;
		ui::renderer& draw = app.draw;
		input_state& input = app.input;
		engine::catalog& schema = *app.db->schema;

		if ( !app.cards_ready )
			place_cards ( app );

		float origin_x = area.x + app.map_x;
		float origin_y = area.y + app.map_y;

		ui::push_clip ( draw, area );

		for ( uint32_t pass = 0; pass < 2; pass++ ) {

			for ( uint32_t t = 0; t < schema.table_count; t++ ) {

				const engine::table_info& child = schema.tables [ t ];

				for ( uint32_t f = 0; f < child.foreign_count; f++ ) {

					const engine::foreign_key& key = child.foreign [ f ];
					engine::table_info* parent = engine::find_table ( schema, key.parent );

					if ( !parent )
						continue;

					uint32_t p = static_cast < uint32_t > ( parent - schema.tables );
					bool lit = app.table == static_cast < int32_t > ( t ) || app.table == static_cast < int32_t > ( p );

					if ( lit != ( pass == 1 ) )
						continue;

					int32_t row = parent_row ( *parent, key );
					float child_x = origin_x + app.cards [ t ].x;
					float parent_x = origin_x + app.cards [ p ].x;
					float child_y = origin_y + app.cards [ t ].y + 36 + static_cast < float > ( key.columns [ 0 ] ) * STUDIO_CARD_ROW + STUDIO_CARD_ROW * 0.5f + 2;
					float parent_y = origin_y + app.cards [ p ].y + ( row >= 0 ? 36 + static_cast < float > ( row ) * STUDIO_CARD_ROW + STUDIO_CARD_ROW * 0.5f + 2 : 18 );
					float gap = parent_x - child_x;
					ui::color shade = lit ? ui::rgb ( THEME_ACCENT ) : ui::rgb ( 0x46505A );
					float width = lit ? 1.6f : 1.2f;
					float from_x = 0;
					float to_x = 0;

					if ( p == t ) {

						from_x = child_x + STUDIO_CARD_WIDTH;
						to_x = from_x;
						ui::loop_curve ( draw, from_x, child_y, to_x, parent_y, 40, shade, width );

					} else if ( gap > STUDIO_CARD_WIDTH + 24 || gap < -STUDIO_CARD_WIDTH - 24 ) {

						from_x = gap > 0 ? child_x + STUDIO_CARD_WIDTH : child_x;
						to_x = gap > 0 ? parent_x : parent_x + STUDIO_CARD_WIDTH;
						ui::curve ( draw, from_x, child_y, to_x, parent_y, shade, width );

					} else {

						float rise = parent_y - child_y;
						float reach = 36 + ( rise < 0 ? -rise : rise ) * 0.12f;

						from_x = child_x;
						to_x = parent_x;
						ui::loop_curve ( draw, from_x, child_y, to_x, parent_y, -( reach < 110 ? reach : 110 ) - ( gap > 0 ? gap : 0 ), shade, width );

					}

					ui::circle ( draw, from_x, child_y, 2.5f, shade );
					ui::circle ( draw, to_x, parent_y, 3.5f, ui::rgb ( THEME_BACKGROUND ) );
					ui::circle ( draw, to_x, parent_y, 2.5f, shade );

				}

			}

		}

		bool on_card = false;

		for ( uint32_t t = 0; t < schema.table_count; t++ ) {

			const engine::table_info& table = schema.tables [ t ];
			ui::rect card = { origin_x + app.cards [ t ].x, origin_y + app.cards [ t ].y, STUDIO_CARD_WIDTH, card_height ( table ) };
			ui::rect header = { card.x, card.y, card.w, 36 };
			uint64_t id = make_id ( "card", t );
			bool selected = app.table == static_cast < int32_t > ( t );

			if ( ui::inside ( card, input.x, input.y ) )
				on_card = true;

			if ( ui::inside ( card, input.x, input.y ) && input.pressed && !ui.active ) {

				ui.active = id;
				app.dragging_card = static_cast < int32_t > ( t );
				app.drag_dx = input.x - card.x;
				app.drag_dy = input.y - card.y;

				if ( !selected )
					select_table ( app, static_cast < int32_t > ( t ) );

			}

			if ( ui.active == id && input.down && app.dragging_card == static_cast < int32_t > ( t ) ) {

				app.cards [ t ].x = input.x - app.drag_dx - origin_x;
				app.cards [ t ].y = input.y - app.drag_dy - origin_y;
				card.x = origin_x + app.cards [ t ].x;
				card.y = origin_y + app.cards [ t ].y;
				header = { card.x, card.y, card.w, 36 };
				ui.cursor = CURSOR_MOVE;
				ui.redraw = true;

			}

			ui::fill_round ( draw, { card.x - 2, card.y + 1, card.w + 4, card.h + 6 }, 9, ui::rgb ( 0, 0.16f ) );
			ui::fill_round ( draw, { card.x - 0.5f, card.y + 1, card.w + 1, card.h + 2.5f }, 7, ui::rgb ( 0, 0.3f ) );
			ui::fill_round ( draw, card, 6, ui::rgb ( THEME_PANEL ) );
			ui::fill_round ( draw, header, 6, ui::rgb ( THEME_CARD_HEADER ) );
			ui::fill ( draw, { header.x, header.y + 28, header.w, 8 }, ui::rgb ( THEME_CARD_HEADER ) );
			ui::line ( draw, card.x, header.y + 36.5f, card.x + card.w, header.y + 36.5f, ui::rgb ( THEME_DIVIDER ) );
			ui::outline_round ( draw, card, 6, ui::rgb ( selected ? THEME_ACCENT : THEME_BORDER ), selected ? 1.5f : 1.0f );
			table_glyph ( draw, header.x + 12, header.y + 12.5f, ui::rgb ( THEME_ACCENT ) );

			base::text rows;

			ensure_stats ( app, static_cast < int32_t > ( t ) );

			if ( app.stats [ t ].known ) {

				grouped ( rows, app.stats [ t ].rows );
				rows.add ( app.stats [ t ].rows == 1 ? " row" : " rows" );

			}

			ui::text ( draw, ui::FONT_STRONG, table.name, { header.x + 32, header.y, header.w - 110, 36 }, ui::rgb ( THEME_TEXT ) );
			ui::text ( draw, ui::FONT_SMALL, rows.value ( ), rows.size, { header.x, header.y, header.w - 12, 36 }, ui::rgb ( THEME_DIM ), ui::ALIGN_RIGHT );

			for ( uint32_t c = 0; c < table.column_count; c++ ) {

				const engine::column_info& column = table.columns [ c ];
				float y = card.y + 40 + static_cast < float > ( c ) * STUDIO_CARD_ROW;
				bool key = column.rowid;
				bool link = false;

				for ( uint32_t k = 0; k < table.primary_count; k++ )
					key = key || table.primary [ k ] == c;

				for ( uint32_t f = 0; f < table.foreign_count; f++ )
					for ( uint32_t k = 0; k < table.foreign [ f ].column_count; k++ )
						link = link || table.foreign [ f ].columns [ k ] == c;

				if ( key )
					ui::text ( draw, ui::FONT_CAPTION, "PK", { card.x + 10, y - 2, 24, STUDIO_CARD_ROW }, ui::rgb ( THEME_ACCENT ) );
				else if ( link )
					ui::text ( draw, ui::FONT_CAPTION, "FK", { card.x + 10, y - 2, 24, STUDIO_CARD_ROW }, ui::rgb ( THEME_FUNCTION ) );

				ui::text ( draw, ui::FONT_SMALL, column.name, { card.x + 34, y - 2, card.w * 0.58f - 34, STUDIO_CARD_ROW }, ui::rgb ( key ? THEME_TEXT : 0xB4BCC3 ) );

				if ( column.type )
					ui::text ( draw, ui::FONT_SMALL, column.type, { card.x, y - 2, card.w - 12, STUDIO_CARD_ROW }, ui::rgb ( THEME_FAINT ), ui::ALIGN_RIGHT );

			}

		}

		uint64_t pan = make_id ( "map.pan" );

		if ( ui::inside ( area, input.x, input.y ) && !on_card && input.pressed && !ui.active ) {

			ui.active = pan;
			app.pan_x = input.x - app.map_x;
			app.pan_y = input.y - app.map_y;

		}

		if ( ui.active == pan && input.down ) {

			app.map_x = input.x - app.pan_x;
			app.map_y = input.y - app.pan_y;
			ui.cursor = CURSOR_MOVE;
			ui.redraw = true;

		}

		if ( ui::inside ( area, input.x, input.y ) && input.wheel != 0 ) {

			if ( input.shift )
				app.map_x += input.wheel * 60;
			else
				app.map_y += input.wheel * 60;

		}

		ui::text ( draw, ui::FONT_SMALL, "Drag tables to arrange them, drag the background to move around", { area.x + 16, area.y + area.h - 30, area.w - 32, 22 }, ui::rgb ( THEME_FAINT ) );
		ui::pop_clip ( draw );

	}

	static void draw_legend ( ui::renderer& draw, float x, float y ) {

		static const uint8_t kinds [ 5 ] = { storage::PAGE_TABLE_INTERIOR, storage::PAGE_TABLE_LEAF, storage::PAGE_INDEX_INTERIOR, storage::PAGE_INDEX_LEAF, storage::PAGE_OVERFLOW };

		for ( uint32_t i = 0; i < 5; i++ ) {

			const char* name = page_kind_name ( kinds [ i ] );

			ui::fill_round ( draw, { x, y + 6, 10, 10 }, 2, page_color ( kinds [ i ] ) );
			ui::text ( draw, ui::FONT_SMALL, name, { x + 16, y, 120, 22 }, ui::rgb ( THEME_DIM ) );
			x += 24 + ui::measure ( draw, ui::FONT_SMALL, name ) + 14;

		}

	}

	static void draw_tree ( studio_state& app, tree_view& tree, const ui::rect& area ) {

		ui_context& ui = app.ui;
		ui::renderer& draw = app.draw;
		input_state& input = app.input;
		float level_h = 64;
		uint32_t level_counts [ 64 ] = { };
		uint32_t leaves = 0;

		for ( uint32_t i = 0; i < tree.count; i++ ) {

			if ( tree.nodes [ i ].level < 64 )
				level_counts [ tree.nodes [ i ].level ]++;

			if ( !tree.nodes [ i ].child_count )
				leaves++;

		}

		float slot = area.w / static_cast < float > ( leaves ? leaves : 1 );
		uint32_t leaf_at = 0;

		for ( uint32_t i = 0; i < tree.count; i++ ) {

			if ( !tree.nodes [ i ].child_count ) {

				tree.nodes [ i ].x = area.x + slot * ( static_cast < float > ( leaf_at ) + 0.5f );
				leaf_at++;

			}

		}

		for ( uint32_t i = tree.count; i-- > 0; ) {

			tree_node& node = tree.nodes [ i ];

			if ( node.child_count ) {

				float first = tree.nodes [ node.first_child ].x;
				float last = tree.nodes [ node.first_child + node.child_count - 1 ].x;

				node.x = ( first + last ) * 0.5f;

			}

		}

		for ( uint32_t i = 0; i < tree.count; i++ ) {

			tree_node& node = tree.nodes [ i ];
			uint32_t count = node.level < 64 ? level_counts [ node.level ] : 1;

			node.w = clamp ( area.w / static_cast < float > ( count ) - 4, 1.5f, 60 );

		}

		for ( uint32_t i = 0; i < tree.count; i++ ) {

			tree_node& node = tree.nodes [ i ];

			if ( node.parent < 0 )
				continue;

			tree_node& parent = tree.nodes [ node.parent ];
			float y1 = area.y + static_cast < float > ( parent.level ) * level_h + 26;
			float y2 = area.y + static_cast < float > ( node.level ) * level_h;

			ui::line ( draw, parent.x, y1, node.x, y2, ui::rgb ( 0x3A424A, node.child_count || tree.leaves < 200 ? 0.9f : 0.45f ) );

		}

		for ( uint32_t i = 0; i < tree.count; i++ ) {

			tree_node& node = tree.nodes [ i ];
			ui::rect box = { node.x - node.w * 0.5f, area.y + static_cast < float > ( node.level ) * level_h, node.w, 26 };
			ui::color shade = page_color ( node.kind );
			float fill = clamp ( static_cast < float > ( node.used ) / static_cast < float > ( PAGE_USABLE ), 0, 1 );
			bool hover = ui::inside ( { box.x - 1, box.y, box.w + 2, box.h }, input.x, input.y ) && !ui.active;

			if ( box.w < 10 ) {

				ui::fill ( draw, box, ui::rgb ( THEME_RAISED ) );
				ui::fill ( draw, { box.x, box.y + box.h * ( 1 - fill ), box.w, box.h * fill }, shade );

			} else {

				ui::fill_round ( draw, box, 3, ui::rgb ( THEME_RAISED ) );
				ui::fill_round ( draw, { box.x + 2, box.y + box.h - 5, ( box.w - 4 ) * fill, 3 }, 1.5f, shade );
				ui::outline_round ( draw, box, 3, hover ? ui::rgb ( THEME_TEXT ) : shade );

				if ( box.w >= 34 ) {

					base::text label;

					label.number ( node.page );
					ui::text ( draw, ui::FONT_CODE_SMALL, label.value ( ), label.size, { box.x, box.y - 2, box.w, box.h - 2 }, ui::rgb ( THEME_TEXT ), ui::ALIGN_CENTER );

				}

			}

			if ( hover ) {

				base::text line;

				line.add ( "Page " ).number ( node.page ).add ( "   " ).add ( page_kind_name ( node.kind ) ).add ( "   " ).number ( node.cells ).add ( node.cells == 1 ? " cell   " : " cells   " ).number ( node.used ).add ( " bytes used, " ).number ( static_cast < uint64_t > ( fill * 100 + 0.5f ) ).add ( "% full" );

				if ( node.has_rowids )
					line.add ( "   row ids " ).signed_number ( node.low ).add ( " to " ).signed_number ( node.high );

				line.add ( "   click to open the page" );
				hover_text ( ui, line.value ( ) );
				ui.cursor = CURSOR_HAND;

				if ( input.pressed ) {

					ui.active = make_id ( "tree.node", node.page );
					app.view = VIEW_PAGES;
					ensure_pages ( app );
					select_page ( app, node.page );
					ui.redraw = true;

				}

			}

		}

	}

	static void draw_storage_map ( studio_state& app, const ui::rect& area ) {

		ui_context& ui = app.ui;
		ui::renderer& draw = app.draw;

		ensure_trees ( app );

		if ( app.table < 0 || !app.tree_count ) {

			ui::text ( draw, ui::FONT_UI, "Pick a table on the left to see how its pages are laid out", area, ui::rgb ( THEME_FAINT ), ui::ALIGN_CENTER );

			return;

		}

		if ( ui::inside ( area, app.input.x, app.input.y ) && app.input.wheel != 0 )
			app.storage_scroll -= app.input.wheel * 80;

		ui::push_clip ( draw, area );

		float y = area.y + 18 - app.storage_scroll;
		float x = area.x + 28;
		float w = area.w - 56;

		for ( uint32_t t = 0; t < app.tree_count; t++ ) {

			tree_view& tree = app.trees [ t ];
			base::text info;
			base::text title;

			title.add ( tree.index ? "Index  " : "Table  " );
			info.number ( tree.levels ).add ( tree.levels == 1 ? " level   " : " levels   " ).number ( tree.count ).add ( tree.count == 1 ? " page   " : " pages   " ).number ( tree.leaves ).add ( tree.leaves == 1 ? " leaf   root page " : " leaves   root page " ).number ( tree.root );

			if ( tree.clipped )
				info.add ( "   too large to draw in full" );

			ui::text ( draw, ui::FONT_CAPTION, tree.index ? "INDEX" : "TABLE", { x, y, 60, 22 }, ui::rgb ( THEME_DIM ) );
			ui::text ( draw, ui::FONT_STRONG, tree.name, { x + 52, y, w * 0.5f, 22 }, ui::rgb ( THEME_TEXT ) );
			ui::text ( draw, ui::FONT_SMALL, info.value ( ), info.size, { x, y, w, 22 }, ui::rgb ( THEME_DIM ), ui::ALIGN_RIGHT );
			y += 34;

			float height = static_cast < float > ( tree.levels ) * 64;

			if ( y + height > area.y && y < area.y + area.h )
				draw_tree ( app, tree, { x, y, w, height } );

			y += height + 6;
			ui::line ( draw, x, y + 0.5f, x + w, y + 0.5f, ui::rgb ( THEME_DIVIDER ) );
			y += 22;

		}

		float content = y + app.storage_scroll - area.y + 40;

		app.storage_scroll = clamp ( app.storage_scroll, 0, content - area.h );
		ui::fill ( draw, { area.x, area.y + area.h - 34, area.w, 34 }, ui::rgb ( THEME_BACKGROUND ) );
		draw_legend ( draw, area.x + 28, area.y + area.h - 30 );
		ui::text ( draw, ui::FONT_SMALL, "bar shows how full each page is", { area.x, area.y + area.h - 30, area.w - 28, 22 }, ui::rgb ( THEME_FAINT ), ui::ALIGN_RIGHT );
		ui::pop_clip ( draw );

	}

	void draw_map_view ( studio_state& app, const ui::rect& area ) {

		ui_context& ui = app.ui;
		bool schema = app.map == MAP_SCHEMA;
		ui::rect body = { area.x, area.y + LAYOUT_HEADER_BAR, area.w, area.h - LAYOUT_HEADER_BAR };

		ui::fill ( app.draw, area, ui::rgb ( THEME_BACKGROUND ) );

		if ( !app.db || !app.db->open || !app.db->schema->table_count ) {

			draw_page_header ( app, area, "Map", "Tables, their links and how their pages are stored", nullptr, 0 );
			ui::text ( app.draw, ui::FONT_UI, "There are no tables to draw yet", body, ui::rgb ( THEME_FAINT ), ui::ALIGN_CENTER );

			return;

		}

		base::text subtitle;

		if ( schema )
			subtitle.add ( "Tables and the foreign keys between them" );
		else if ( app.table >= 0 )
			subtitle.add ( "B-trees of " ).add ( app.db->schema->tables [ app.table ].name ).add ( " and its indexes, page by page" );

		draw_page_header ( app, area, schema ? "Schema map" : "Storage map", subtitle.value ( ), nullptr, 0 );

		float right = area.x + area.w - LAYOUT_MARGIN;
		float y = area.y + ( LAYOUT_HEADER_BAR - LAYOUT_BUTTON ) * 0.5f;

		if ( segment ( ui, make_id ( "map.storage" ), { right - 110, y, 110, LAYOUT_BUTTON }, "Storage", !schema, false, true ) )
			app.map = MAP_STORAGE;

		if ( segment ( ui, make_id ( "map.schema" ), { right - 220, y, 110, LAYOUT_BUTTON }, "Schema", schema, true, false ) )
			app.map = MAP_SCHEMA;

		if ( app.map == MAP_SCHEMA )
			draw_schema_map ( app, body );
		else
			draw_storage_map ( app, body );

	}

}
