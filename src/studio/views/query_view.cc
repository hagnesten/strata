#include "studio/app/studio.hh"

namespace studio {

	static bool starts ( const char* text, const char* prefix ) {

		return base::starts_with ( text, prefix );

	}

	void draw_plan ( studio_state& app, const ui::rect& area ) {

		ui::renderer& draw = app.draw;
		plan_data& plan = app.plan;

		ui::fill ( draw, area, ui::rgb ( THEME_BACKGROUND ) );

		if ( !plan.count ) {

			ui::text ( draw, ui::FONT_UI, "Press Explain to see how Strata will run the query", area, ui::rgb ( THEME_FAINT ), ui::ALIGN_CENTER );

			return;

		}

		ui::push_clip ( draw, area );

		float top = area.y + 20;
		float row = 40;
		uint32_t scans = 0;
		uint32_t searches = 0;

		for ( uint32_t i = 0; i < plan.count; i++ ) {

			const plan_row& step = plan.rows [ i ];
			float x = area.x + 24 + static_cast < float > ( step.depth ) * 30;
			float y = top + static_cast < float > ( i ) * row;
			bool scan = starts ( step.detail, "SCAN" ) && !starts ( step.detail, "SCAN CONSTANT" );
			bool search = starts ( step.detail, "SEARCH" );
			uint32_t mark = scan ? THEME_NUMBER : search ? THEME_ACCENT : THEME_FAINT;
			ui::rect box = { x, y, base::min ( area.w - ( x - area.x ) - 24, 760.0f ), 30 };

			scans += scan;
			searches += search;

			for ( uint32_t k = i; k-- > 0; ) {

				if ( plan.rows [ k ].id != step.parent )
					continue;

				float parent_x = area.x + 24 + static_cast < float > ( plan.rows [ k ].depth ) * 30 + 12;
				float parent_y = top + static_cast < float > ( k ) * row + 30;

				ui::line ( draw, parent_x, parent_y, parent_x, y + 15, ui::rgb ( THEME_BORDER ) );
				ui::line ( draw, parent_x, y + 15, x, y + 15, ui::rgb ( THEME_BORDER ) );

				break;

			}

			ui::fill_round ( draw, box, LAYOUT_RADIUS, ui::rgb ( THEME_RAISED ) );
			ui::outline_round ( draw, box, LAYOUT_RADIUS, ui::rgb ( THEME_BORDER ) );
			ui::fill ( draw, { box.x + 1, box.y + 6, 3, box.h - 12 }, ui::rgb ( mark ) );
			ui::text ( draw, ui::FONT_CODE, step.detail, { box.x + 14, box.y, box.w - 24, box.h }, ui::rgb ( THEME_TEXT ) );

		}

		float bottom = top + static_cast < float > ( plan.count ) * row + 12;
		base::text summary;

		summary.number ( plan.count ).add ( plan.count == 1 ? " step   " : " steps   " ).number ( searches ).add ( searches == 1 ? " search with a key   " : " searches with a key   " ).number ( scans ).add ( scans == 1 ? " full scan" : " full scans" );
		ui::text ( draw, ui::FONT_SMALL, summary.value ( ), summary.size, { area.x + 24, bottom, area.w - 48, 20 }, ui::rgb ( THEME_DIM ) );

		float legend = bottom + 28;

		ui::fill ( draw, { area.x + 24, legend + 6, 3, 10 }, ui::rgb ( THEME_ACCENT ) );
		ui::text ( draw, ui::FONT_SMALL, "uses a row id or an index", { area.x + 34, legend, 200, 22 }, ui::rgb ( THEME_DIM ) );
		ui::fill ( draw, { area.x + 240, legend + 6, 3, 10 }, ui::rgb ( THEME_NUMBER ) );
		ui::text ( draw, ui::FONT_SMALL, "reads every row", { area.x + 250, legend, 200, 22 }, ui::rgb ( THEME_DIM ) );
		ui::pop_clip ( draw );

	}

	void draw_query_view ( studio_state& app, const ui::rect& area ) {

		ui_context& ui = app.ui;
		ui::renderer& draw = app.draw;
		float editor_h = clamp ( app.editor_height, 90, area.h - 140 );
		ui::rect editor = { area.x, area.y, area.w, editor_h };
		ui::rect strip = { area.x, area.y + editor_h, area.w, 34 };
		ui::rect results = { area.x, strip.y + strip.h, area.w, area.h - editor_h - strip.h };

		if ( draw_editor ( ui, app.editor, editor ) )
			run_editor ( app, false );

		float split = area.y + editor_h;

		if ( splitter ( ui, make_id ( "query.split" ), { area.x, split - 3, area.w, 6 }, false, split, area.y + 90, area.y + area.h - 140 ) )
			app.editor_height = split - area.y;

		ui::fill ( draw, strip, ui::rgb ( THEME_PANEL ) );
		ui::line ( draw, strip.x, strip.y + 0.5f, strip.x + strip.w, strip.y + 0.5f, ui::rgb ( THEME_DIVIDER ) );
		ui::line ( draw, strip.x, strip.y + strip.h - 0.5f, strip.x + strip.w, strip.y + strip.h - 0.5f, ui::rgb ( THEME_DIVIDER ) );
		caption ( ui, { strip.x + 16, strip.y, 80, strip.h }, app.result == RESULT_PLAN ? "Plan" : "Result" );

		if ( app.result_message.size && app.result != RESULT_MESSAGE )
			ui::text ( draw, ui::FONT_SMALL, app.result_message.value ( ), app.result_message.size, { strip.x + 90, strip.y, strip.w - 300, strip.h }, ui::rgb ( THEME_DIM ) );

		float right = strip.x + strip.w - 12;
		bool has_grid = app.result_grid.loaded;
		bool has_plan = app.plan.count > 0;

		if ( has_grid && has_plan ) {

			if ( segment ( ui, make_id ( "result.plan" ), { right - 74, strip.y + 6, 74, strip.h - 12 }, "Plan", app.result == RESULT_PLAN, false, true ) )
				app.result = RESULT_PLAN;

			if ( segment ( ui, make_id ( "result.grid" ), { right - 148, strip.y + 6, 74, strip.h - 12 }, "Rows", app.result == RESULT_GRID, true, false ) )
				app.result = RESULT_GRID;

		}

		switch ( app.result ) {

			case RESULT_GRID:
				draw_grid ( ui, app.result_grid, results, "The query returned no columns" );
				break;

			case RESULT_PLAN:
				draw_plan ( app, results );
				break;

			case RESULT_MESSAGE: {

				ui::fill ( draw, results, ui::rgb ( THEME_BACKGROUND ) );

				ui::rect text = { results.x + 24, results.y + 20, results.w - 48, 22 };

				if ( app.result_failed ) {

					ui::fill ( draw, { text.x, text.y + 2, 3, 40 }, ui::rgb ( THEME_ERROR ) );
					ui::text ( draw, ui::FONT_STRONG, "The statement failed", { text.x + 14, text.y, text.w - 14, 20 }, ui::rgb ( THEME_ERROR ) );
					ui::text ( draw, ui::FONT_CODE, app.result_message.value ( ), app.result_message.size, { text.x + 14, text.y + 22, text.w - 14, 22 }, ui::rgb ( THEME_TEXT ) );

				} else {

					ui::fill ( draw, { text.x, text.y + 2, 3, 18 }, ui::rgb ( THEME_SUCCESS ) );
					ui::text ( draw, ui::FONT_UI, app.result_message.value ( ), app.result_message.size, { text.x + 14, text.y, text.w - 14, 22 }, ui::rgb ( THEME_TEXT ) );

				}

				break;

			}

			default:
				ui::fill ( draw, results, ui::rgb ( THEME_BACKGROUND ) );
				ui::text ( draw, ui::FONT_UI, "Write SQL above and press Run. Select part of the text to run only that.", results, ui::rgb ( THEME_FAINT ), ui::ALIGN_CENTER );
				break;

		}

	}

}
