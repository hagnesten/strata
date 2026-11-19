#include "studio/widgets/icons.hh"

namespace studio {

	void draw_icon ( ui::renderer& draw, icon_kind kind, float x, float y, ui::color shade ) {

		const float w = 1.35f;
		auto segment = [ & ] ( float x1, float y1, float x2, float y2 ) {

			ui::line ( draw, x + x1, y + y1, x + x2, y + y2, shade, w );

		};

		switch ( kind ) {

			case ICON_DATA:
			case ICON_TABLE:
				ui::outline_round ( draw, { x + 1.5f, y + 2.5f, 13, 11 }, 2, shade, w );
				segment ( 1.5f, 6.5f, 14.5f, 6.5f );
				segment ( 1.5f, 10, 14.5f, 10 );
				segment ( 6, 6.5f, 6, 13.5f );
				break;

			case ICON_QUERY:
				ui::outline_round ( draw, { x + 1, y + 2, 14, 12 }, 2, shade, w );
				segment ( 4, 6, 6.5f, 8 );
				segment ( 6.5f, 8, 4, 10 );
				segment ( 8, 10.5f, 12, 10.5f );
				break;

			case ICON_MAP:
				ui::ring ( draw, x + 4, y + 4, 2.3f, shade, w );
				ui::ring ( draw, x + 12, y + 4.5f, 2.3f, shade, w );
				ui::ring ( draw, x + 7.5f, y + 12, 2.3f, shade, w );
				segment ( 6.2f, 4.2f, 9.7f, 4.4f );
				segment ( 5, 6, 6.6f, 9.9f );
				segment ( 11, 6.6f, 8.6f, 10 );
				break;

			case ICON_PAGES:
				ui::outline_round ( draw, { x + 1.5f, y + 1.5f, 5.5f, 5.5f }, 1, shade, w );
				ui::outline_round ( draw, { x + 9, y + 1.5f, 5.5f, 5.5f }, 1, shade, w );
				ui::outline_round ( draw, { x + 1.5f, y + 9, 5.5f, 5.5f }, 1, shade, w );
				ui::fill_round ( draw, { x + 9, y + 9, 5.5f, 5.5f }, 1, shade );
				break;

			case ICON_INDEX:
				segment ( 2, 3.5f, 14, 3.5f );
				segment ( 2, 8, 11, 8 );
				segment ( 2, 12.5f, 7.5f, 12.5f );
				break;

			case ICON_PLAY:
				segment ( 4.5f, 2.5f, 4.5f, 13.5f );
				segment ( 4.5f, 2.5f, 13, 8 );
				segment ( 13, 8, 4.5f, 13.5f );
				break;

			case ICON_PLAN:
				ui::outline_round ( draw, { x + 1.5f, y + 1.5f, 6, 4 }, 1, shade, w );
				ui::outline_round ( draw, { x + 8.5f, y + 10.5f, 6, 4 }, 1, shade, w );
				segment ( 4.5f, 5.5f, 4.5f, 12.5f );
				segment ( 4.5f, 12.5f, 8.5f, 12.5f );
				break;

			case ICON_SAVE:
				ui::outline_round ( draw, { x + 2, y + 2, 12, 12 }, 1.5f, shade, w );
				ui::outline ( draw, { x + 5, y + 2, 6, 4 }, shade, w );
				ui::outline ( draw, { x + 4.5f, y + 9, 7, 5 }, shade, w );
				break;

			case ICON_HISTORY:
				ui::ring ( draw, x + 8, y + 8, 6, shade, w );
				segment ( 8, 4.5f, 8, 8 );
				segment ( 8, 8, 10.5f, 9.5f );
				break;

			case ICON_OPEN:
				segment ( 1.5f, 3.5f, 6, 3.5f );
				segment ( 6, 3.5f, 7.5f, 5 );
				segment ( 7.5f, 5, 14.5f, 5 );
				segment ( 14.5f, 5, 14.5f, 13 );
				segment ( 14.5f, 13, 1.5f, 13 );
				segment ( 1.5f, 13, 1.5f, 3.5f );
				segment ( 1.5f, 7.5f, 14.5f, 7.5f );
				break;

			case ICON_IMPORT:
				segment ( 8, 1.5f, 8, 10 );
				segment ( 4.5f, 6.5f, 8, 10 );
				segment ( 11.5f, 6.5f, 8, 10 );
				segment ( 2, 10.5f, 2, 14 );
				segment ( 2, 14, 14, 14 );
				segment ( 14, 14, 14, 10.5f );
				break;

			case ICON_EXPORT:
				segment ( 8, 10, 8, 1.5f );
				segment ( 4.5f, 5, 8, 1.5f );
				segment ( 11.5f, 5, 8, 1.5f );
				segment ( 2, 10.5f, 2, 14 );
				segment ( 2, 14, 14, 14 );
				segment ( 14, 14, 14, 10.5f );
				break;

			case ICON_PLUS:
				segment ( 8, 2.5f, 8, 13.5f );
				segment ( 2.5f, 8, 13.5f, 8 );
				break;

			case ICON_TRASH:
				segment ( 2, 4, 14, 4 );
				segment ( 6, 4, 6, 2 );
				segment ( 6, 2, 10, 2 );
				segment ( 10, 2, 10, 4 );
				ui::outline_round ( draw, { x + 3.5f, y + 4, 9, 10.5f }, 1.5f, shade, w );
				segment ( 6.5f, 7, 6.5f, 11.5f );
				segment ( 9.5f, 7, 9.5f, 11.5f );
				break;

			case ICON_EDIT:
				segment ( 2.5f, 13.5f, 3.2f, 10.5f );
				segment ( 3.2f, 10.5f, 11, 2.7f );
				segment ( 11, 2.7f, 13.3f, 5 );
				segment ( 13.3f, 5, 5.5f, 12.8f );
				segment ( 5.5f, 12.8f, 2.5f, 13.5f );
				break;

			case ICON_CHECK:
				segment ( 8, 1.5f, 14, 4 );
				segment ( 14, 4, 13, 10 );
				segment ( 13, 10, 8, 14.5f );
				segment ( 8, 14.5f, 3, 10 );
				segment ( 3, 10, 2, 4 );
				segment ( 2, 4, 8, 1.5f );
				segment ( 5.3f, 8, 7.3f, 10 );
				segment ( 7.3f, 10, 10.8f, 6 );
				break;

			case ICON_CHART:
				segment ( 2, 14, 14.5f, 14 );
				ui::outline ( draw, { x + 3, y + 8, 2.5f, 6 }, shade, w );
				ui::outline ( draw, { x + 7, y + 4, 2.5f, 10 }, shade, w );
				ui::outline ( draw, { x + 11, y + 6.5f, 2.5f, 7.5f }, shade, w );
				break;

			case ICON_REFRESH:
				segment ( 13, 3, 13, 6.5f );
				segment ( 13, 6.5f, 9.5f, 6.5f );
				segment ( 12.4f, 6, 10.5f, 3.4f );
				segment ( 10.5f, 3.4f, 7.5f, 2.6f );
				segment ( 7.5f, 2.6f, 4.4f, 3.8f );
				segment ( 4.4f, 3.8f, 2.6f, 6.6f );
				segment ( 2.6f, 6.6f, 2.6f, 9.6f );
				segment ( 2.6f, 9.6f, 4.4f, 12.3f );
				segment ( 4.4f, 12.3f, 7.5f, 13.4f );
				segment ( 7.5f, 13.4f, 10.5f, 12.6f );
				segment ( 10.5f, 12.6f, 12.6f, 10.2f );
				break;

			case ICON_CLOSE:
				segment ( 3.5f, 3.5f, 12.5f, 12.5f );
				segment ( 12.5f, 3.5f, 3.5f, 12.5f );
				break;

			case ICON_MINIMIZE:
				segment ( 3, 8.5f, 13, 8.5f );
				break;

			case ICON_MAXIMIZE:
				ui::outline ( draw, { x + 3.5f, y + 3.5f, 9, 9 }, shade, 1.0f );
				break;

			case ICON_RESTORE:
				ui::outline ( draw, { x + 3.5f, y + 5.5f, 7, 7 }, shade, 1.0f );
				segment ( 5.5f, 5.5f, 5.5f, 3.5f );
				segment ( 5.5f, 3.5f, 12.5f, 3.5f );
				segment ( 12.5f, 3.5f, 12.5f, 10.5f );
				segment ( 12.5f, 10.5f, 10.5f, 10.5f );
				break;

			case ICON_LOGO:
				ui::fill_round ( draw, { x + 1, y + 2, 14, 3 }, 1, shade );
				ui::fill_round ( draw, { x + 3, y + 6.5f, 12, 3 }, 1, ui::mix ( shade, ui::rgb ( THEME_CHROME ), 0.3f ) );
				ui::fill_round ( draw, { x + 1, y + 11, 10, 3 }, 1, ui::mix ( shade, ui::rgb ( THEME_CHROME ), 0.55f ) );
				break;

			case ICON_FILE:
				segment ( 3, 1.5f, 9.5f, 1.5f );
				segment ( 9.5f, 1.5f, 13, 5 );
				segment ( 13, 5, 13, 14.5f );
				segment ( 13, 14.5f, 3, 14.5f );
				segment ( 3, 14.5f, 3, 1.5f );
				segment ( 9.5f, 1.5f, 9.5f, 5 );
				segment ( 9.5f, 5, 13, 5 );
				break;

			case ICON_UP:
				segment ( 8, 13.5f, 8, 2.5f );
				segment ( 3.5f, 7, 8, 2.5f );
				segment ( 12.5f, 7, 8, 2.5f );
				break;

			case ICON_DRIVE:
				ui::outline_round ( draw, { x + 1.5f, y + 4.5f, 13, 7 }, 1.5f, shade, w );
				ui::circle ( draw, x + 11.5f, y + 8, 1, shade );
				break;

			case ICON_KEY:
				ui::ring ( draw, x + 5, y + 8, 3, shade, w );
				segment ( 8, 8, 14.5f, 8 );
				segment ( 12.5f, 8, 12.5f, 11 );
				segment ( 14.5f, 8, 14.5f, 10.5f );
				break;

			case ICON_ROWS:
				segment ( 2, 4, 14, 4 );
				segment ( 2, 8, 14, 8 );
				segment ( 2, 12, 14, 12 );
				break;

		}

	}

	float icon_button_width ( ui::renderer& draw, const char* label ) {

		return ICON_SIZE + ( label && *label ? 8 + ui::measure ( draw, ui::FONT_STRONG, label ) : 0 ) + 28;

	}

	bool icon_button ( ui_context& ui, uint64_t id, const ui::rect& area, icon_kind icon, const char* label, bool primary, bool enabled ) {

		ui::renderer& draw = *ui.draw;
		bool hover = enabled && ui::inside ( area, ui.input->x, ui.input->y ) && ( !ui.active || ui.active == id );

		if ( hover ) {

			ui.hot = id;
			ui.cursor = CURSOR_HAND;

		}

		if ( hover && ui.input->pressed )
			ui.active = id;

		bool held = ui.active == id && ui.input->down;
		bool clicked = enabled && ui.active == id && ui.input->released && hover;
		ui::color fill = ui::rgb ( primary ? THEME_ACCENT_DEEP : THEME_RAISED );
		ui::color ink = ui::rgb ( !enabled ? THEME_FAINT : primary ? 0xF2F6F6 : THEME_TEXT );

		if ( held )
			fill = ui::rgb ( primary ? 0x276860 : THEME_PRESSED );
		else if ( hover )
			fill = ui::rgb ( primary ? 0x35877F : THEME_HOVER );

		if ( !enabled )
			fill = ui::rgb ( THEME_PANEL );

		ui::fill_round ( draw, area, LAYOUT_RADIUS, fill );

		if ( !primary )
			ui::outline_round ( draw, area, LAYOUT_RADIUS, ui::rgb ( enabled ? THEME_BORDER : THEME_DIVIDER ) );

		float label_w = label && *label ? ui::measure ( draw, ui::FONT_STRONG, label ) + 8 : 0;
		float x = area.x + ( area.w - ICON_SIZE - label_w ) * 0.5f;

		draw_icon ( draw, icon, x, area.y + ( area.h - ICON_SIZE ) * 0.5f, primary ? ink : ui::rgb ( enabled ? THEME_DIM : THEME_FAINT ) );

		if ( label_w > 0 )
			ui::text ( draw, ui::FONT_STRONG, label, { x + ICON_SIZE + 8, area.y, label_w, area.h }, ink );

		if ( clicked )
			ui.redraw = true;

		return clicked;

	}

	bool icon_only ( ui_context& ui, uint64_t id, const ui::rect& area, icon_kind icon, const char* tip, bool enabled ) {

		ui::renderer& draw = *ui.draw;
		bool hover = enabled && ui::inside ( area, ui.input->x, ui.input->y ) && ( !ui.active || ui.active == id );

		if ( hover ) {

			ui.hot = id;
			ui.cursor = CURSOR_HAND;

			if ( tip )
				hover_text ( ui, tip );

		}

		if ( hover && ui.input->pressed )
			ui.active = id;

		bool clicked = enabled && ui.active == id && ui.input->released && hover;

		if ( hover )
			ui::fill_round ( draw, area, LAYOUT_RADIUS, ui::rgb ( ui.active == id ? THEME_PRESSED : THEME_HOVER ) );

		draw_icon ( draw, icon, area.x + ( area.w - ICON_SIZE ) * 0.5f, area.y + ( area.h - ICON_SIZE ) * 0.5f, ui::rgb ( !enabled ? THEME_FAINT : hover ? THEME_TEXT : THEME_DIM ) );

		if ( clicked )
			ui.redraw = true;

		return clicked;

	}

}