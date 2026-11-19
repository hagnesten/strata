#include "studio/app/studio.hh"

namespace studio {

	void draw_plan ( studio_state& app, const ui::rect& area ) {

		ui::renderer& draw = app.draw;
		plan_data& plan = app.plan;

		ui::fill ( draw, area, ui::rgb ( THEME_BACKGROUND ) );

		if ( !plan.count ) {

			ui::text ( draw, ui::FONT_UI, "Press Explain to see how Strata will run the query", area, ui::rgb ( THEME_FAINT ), ui::ALIGN_CENTER );

			return;

		}

		ui::push_clip ( draw, area );

		float top = area.y + 24;
		float row = 44;
		uint32_t scans = 0;
		uint32_t searches = 0;

		for ( uint32_t i = 0; i < plan.count; i++ ) {

			const plan_row& step = plan.rows [ i ];
			float x = area.x + LAYOUT_MARGIN + static_cast < float > ( step.depth ) * 32;
			float y = top + static_cast < float > ( i ) * row;
			bool scan = base::starts_with ( step.detail, "SCAN" ) && !base::starts_with ( step.detail, "SCAN CONSTANT" );
			bool search = base::starts_with ( step.detail, "SEARCH" );
			uint32_t mark = scan ? THEME_NUMBER : search ? THEME_ACCENT : THEME_FAINT;
			ui::rect box = { x, y, base::min ( area.w - ( x - area.x ) - LAYOUT_MARGIN, 820.0f ), 34 };

			scans += scan;
			searches += search;

			for ( uint32_t k = i; k-- > 0; ) {

				if ( plan.rows [ k ].id != step.parent )
					continue;

				float parent_x = area.x + LAYOUT_MARGIN + static_cast < float > ( plan.rows [ k ].depth ) * 32 + 14;
				float parent_y = top + static_cast < float > ( k ) * row + 34;

				ui::line ( draw, parent_x, parent_y, parent_x, y + 17, ui::rgb ( THEME_BORDER ) );
				ui::line ( draw, parent_x, y + 17, x, y + 17, ui::rgb ( THEME_BORDER ) );

				break;

			}

			ui::fill_round ( draw, box, LAYOUT_RADIUS, ui::rgb ( THEME_RAISED ) );
			ui::outline_round ( draw, box, LAYOUT_RADIUS, ui::rgb ( THEME_BORDER ) );
			ui::fill_round ( draw, { box.x + 1, box.y + 7, 3, box.h - 14 }, 1.5f, ui::rgb ( mark ) );
			ui::text ( draw, ui::FONT_CODE, step.detail, { box.x + 16, box.y, box.w - 28, box.h }, ui::rgb ( THEME_TEXT ) );

		}

		float bottom = top + static_cast < float > ( plan.count ) * row + 14;
		base::text summary;

		summary.number ( plan.count ).add ( plan.count == 1 ? " step   " : " steps   " ).number ( searches ).add ( searches == 1 ? " search with a key   " : " searches with a key   " ).number ( scans ).add ( scans == 1 ? " full scan" : " full scans" );
		ui::text ( draw, ui::FONT_SMALL, summary.value ( ), summary.size, { area.x + LAYOUT_MARGIN, bottom, area.w - 56, 20 }, ui::rgb ( THEME_DIM ) );

		float legend = bottom + 30;

		ui::fill_round ( draw, { area.x + LAYOUT_MARGIN, legend + 5, 3, 12 }, 1.5f, ui::rgb ( THEME_ACCENT ) );
		ui::text ( draw, ui::FONT_SMALL, "uses a row id or an index", { area.x + LAYOUT_MARGIN + 12, legend, 220, 22 }, ui::rgb ( THEME_DIM ) );
		ui::fill_round ( draw, { area.x + LAYOUT_MARGIN + 240, legend + 5, 3, 12 }, 1.5f, ui::rgb ( THEME_NUMBER ) );
		ui::text ( draw, ui::FONT_SMALL, "reads every row", { area.x + LAYOUT_MARGIN + 252, legend, 200, 22 }, ui::rgb ( THEME_DIM ) );
		ui::pop_clip ( draw );

	}

	static void draw_message ( studio_state& app, const ui::rect& results ) {

		ui::renderer& draw = app.draw;
		ui::rect text = { results.x + LAYOUT_MARGIN, results.y + 24, results.w - LAYOUT_MARGIN * 2, 22 };

		ui::fill ( draw, results, ui::rgb ( THEME_BACKGROUND ) );

		if ( app.result_failed ) {

			ui::fill_round ( draw, { text.x, text.y + 2, 3, 42 }, 1.5f, ui::rgb ( THEME_ERROR ) );
			ui::text ( draw, ui::FONT_STRONG, "The statement failed", { text.x + 16, text.y, text.w - 16, 22 }, ui::rgb ( THEME_ERROR ) );
			ui::text ( draw, ui::FONT_CODE, app.result_message.value ( ), app.result_message.size, { text.x + 16, text.y + 24, text.w - 16, 22 }, ui::rgb ( THEME_TEXT ) );

		} else {

			ui::fill_round ( draw, { text.x, text.y + 2, 3, 18 }, 1.5f, ui::rgb ( THEME_SUCCESS ) );
			ui::text ( draw, ui::FONT_UI, app.result_message.value ( ), app.result_message.size, { text.x + 16, text.y, text.w - 16, 22 }, ui::rgb ( THEME_TEXT ) );

		}

	}

	void draw_query_view ( studio_state& app, const ui::rect& area ) {

		ui_context& ui = app.ui;
		ui::renderer& draw = app.draw;

		completion_click ( ui, app.editor, area );

		header_action actions [ 5 ] = {

			{ ICON_HISTORY, "History", false, true },
			{ ICON_SAVE, "Save", false, app.editor.text.size > 0 },
			{ ICON_EXPORT, "Export", false, app.result_sql.size > 0 },
			{ ICON_PLAN, "Explain", false, app.db != nullptr },
			{ ICON_PLAY, "Run", true, app.db != nullptr },

		};

		switch ( draw_page_header ( app, area, "Query", "Ctrl+Enter runs the editor or the selected part   Ctrl+T opens a tab", actions, 5 ) ) {

			case 0:
				app.library.page = app.library.page == LIBRARY_HIDDEN ? LIBRARY_HISTORY : LIBRARY_HIDDEN;
				app.library.naming = false;
				break;

			case 1:
				app.library.page = LIBRARY_SAVED;
				app.library.naming = true;
				set_field ( app.library.save_name, "" );
				focus_field ( ui, app.library.save_name );
				break;

			case 2:
				open_export ( app, app.result_sql.value ( ), "result" );
				break;

			case 3:
				run_editor ( app, true );
				break;

			case 4:
				run_editor ( app, false );
				break;

			default:
				break;

		}

		float top = area.y + LAYOUT_HEADER_BAR;
		float panel = app.library.page != LIBRARY_HIDDEN ? LIBRARY_WIDTH : 0;
		ui::rect body = { area.x, top, area.w - panel, area.y + area.h - top };

		if ( panel > 0 )
			draw_library ( app, { area.x + area.w - panel, top, panel, body.h } );

		draw_query_tabs ( app, { body.x, body.y, body.w, QUERY_TABS_HEIGHT } );

		float editor_top = body.y + QUERY_TABS_HEIGHT;
		float editor_h = clamp ( app.editor_height, 90, body.h - QUERY_TABS_HEIGHT - 160 );
		ui::rect editor = { body.x, editor_top, body.w, editor_h };
		ui::rect strip = { body.x, editor_top + editor_h, body.w, 42 };
		ui::rect results = { body.x, strip.y + strip.h, body.w, body.y + body.h - strip.y - strip.h };

		if ( draw_editor ( ui, app.editor, editor ) )
			run_editor ( app, false );

		float split = editor_top + editor_h;

		if ( splitter ( ui, make_id ( "query.split" ), { body.x, split - 3, body.w, 6 }, false, split, editor_top + 90, body.y + body.h - 160 ) )
			app.editor_height = split - editor_top;

		ui::fill ( draw, strip, ui::rgb ( THEME_PANEL ) );
		ui::line ( draw, strip.x, strip.y + 0.5f, strip.x + strip.w, strip.y + 0.5f, ui::rgb ( THEME_DIVIDER ) );
		ui::line ( draw, strip.x, strip.y + strip.h - 0.5f, strip.x + strip.w, strip.y + strip.h - 0.5f, ui::rgb ( THEME_DIVIDER ) );
		caption ( ui, { strip.x + LAYOUT_MARGIN, strip.y, 90, strip.h }, app.result == RESULT_PLAN ? "Plan" : app.result == RESULT_CHART ? "Chart" : "Result" );

		if ( app.result_message.size && app.result != RESULT_MESSAGE )
			ui::text ( draw, ui::FONT_SMALL, app.result_message.value ( ), app.result_message.size, { strip.x + LAYOUT_MARGIN + 80, strip.y, strip.w - 420, strip.h }, ui::rgb ( THEME_DIM ) );

		float right = strip.x + strip.w - 14;
		bool has_grid = app.result_grid.loaded && app.result_grid.column_count;
		bool has_plan = app.plan.count > 0;
		uint32_t count = ( has_grid ? 2 : 0 ) + ( has_plan ? 1 : 0 );

		if ( count > 1 ) {

			float x = right - static_cast < float > ( count ) * 78;
			uint32_t at = 0;

			if ( has_grid ) {

				if ( segment ( ui, make_id ( "result.grid" ), { x, strip.y + 7, 78, strip.h - 14 }, "Rows", app.result == RESULT_GRID, true, false ) )
					app.result = RESULT_GRID;

				if ( segment ( ui, make_id ( "result.chart" ), { x + 78, strip.y + 7, 78, strip.h - 14 }, "Chart", app.result == RESULT_CHART, false, !has_plan ) )
					app.result = RESULT_CHART;

				at = 2;

			}

			if ( has_plan && segment ( ui, make_id ( "result.plan" ), { x + static_cast < float > ( at ) * 78, strip.y + 7, 78, strip.h - 14 }, "Plan", app.result == RESULT_PLAN, at == 0, true ) )
				app.result = RESULT_PLAN;

		}

		switch ( app.result ) {

			case RESULT_GRID:
				draw_grid ( ui, app.result_grid, results, "The query returned no columns" );
				break;

			case RESULT_CHART:
				draw_chart ( ui, app.chart, app.result_grid, results );
				break;

			case RESULT_PLAN:
				draw_plan ( app, results );
				break;

			case RESULT_MESSAGE:
				draw_message ( app, results );
				break;

			default:
				ui::fill ( draw, results, ui::rgb ( THEME_BACKGROUND ) );
				ui::text ( draw, ui::FONT_UI, "Write SQL above and press Run. Select part of the text to run only that.", results, ui::rgb ( THEME_FAINT ), ui::ALIGN_CENTER );
				break;

		}

		draw_completion ( ui, app.editor, area );

	}

}