#include "studio/app/studio.hh"

namespace studio {

	void draw_check_overlay ( studio_state& app, const ui::rect& area ) {

		ui_context& ui = app.ui;
		ui::renderer& draw = app.draw;
		engine::check_report& report = app.check;
		ui::rect inner = draw_overlay_frame ( app, area, 820, report.problem_count ? 560 : 320, "Integrity check" );
		bool clean = report.done && !report.problem_count;
		float y = inner.y;
		base::text value;
		float w = inner.w / 4;

		ui::fill_round ( draw, { inner.x, y + 4, 3, 40 }, 1.5f, ui::rgb ( clean ? THEME_SUCCESS : THEME_ERROR ) );
		ui::text ( draw, ui::FONT_STRONG, clean ? "The file is consistent" : "The check found problems", { inner.x + 16, y, inner.w - 16, 24 }, ui::rgb ( clean ? THEME_TEXT : THEME_ERROR ) );
		ui::text ( draw, ui::FONT_SMALL, "Every B-tree was walked page by page, the free list was followed, and every index entry was matched to its row", { inner.x + 16, y + 24, inner.w - 16, 20 }, ui::rgb ( THEME_DIM ) );
		y += 52;
		ui::line ( draw, inner.x, y + 0.5f, inner.x + inner.w, y + 0.5f, ui::rgb ( THEME_DIVIDER ) );

		grouped ( value, report.pages );
		draw_stat ( app, inner.x, y, w, "Pages", value.value ( ), nullptr, true );
		value.clear ( );
		value.number ( report.trees );
		draw_stat ( app, inner.x + w, y, w, "Trees", value.value ( ), nullptr, true );
		value.clear ( );
		grouped ( value, report.rows );
		draw_stat ( app, inner.x + w * 2, y, w, "Rows", value.value ( ), nullptr, true );
		value.clear ( );
		value.number ( report.problem_count );
		draw_stat ( app, inner.x + w * 3, y, w, "Problems", value.value ( ), nullptr, false );
		y += LAYOUT_STATS - 14;
		ui::line ( draw, inner.x, y + 0.5f, inner.x + inner.w, y + 0.5f, ui::rgb ( THEME_DIVIDER ) );
		y += 14;

		ui::rect list = { inner.x, y, inner.w, inner.y + inner.h - y };

		if ( !report.problem_count ) {

			ui::text ( draw, ui::FONT_SMALL, "Nothing to fix. Run the check again after big changes or a crash.", { list.x, list.y, list.w, 24 }, ui::rgb ( THEME_FAINT ) );

			return;

		}

		if ( ui::inside ( list, app.input.x, app.input.y ) && app.input.wheel != 0 )
			app.check_scroll -= app.input.wheel * 30 * 3;

		float content = static_cast < float > ( report.problem_count ) * 30;

		app.check_scroll = clamp ( app.check_scroll, 0, content - list.h );
		ui::push_clip ( draw, list );

		for ( uint32_t i = 0; i < report.problem_count; i++ ) {

			float ry = list.y + static_cast < float > ( i ) * 30 - app.check_scroll;

			if ( ry + 30 < list.y || ry > list.y + list.h )
				continue;

			ui::circle ( draw, list.x + 6, ry + 15, 3, ui::rgb ( THEME_ERROR ) );
			ui::text ( draw, ui::FONT_UI, report.problems [ i ], { list.x + 20, ry, list.w - 30, 30 }, ui::rgb ( THEME_TEXT ) );

		}

		ui::pop_clip ( draw );
		app.check_scroll = scrollbar ( ui, make_id ( "check.scroll" ), { list.x + list.w - LAYOUT_SCROLLBAR, list.y, LAYOUT_SCROLLBAR, list.h }, content, list.h, app.check_scroll, true );

	}

}