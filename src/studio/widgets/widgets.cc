#include "studio/widgets/widgets.hh"

namespace studio {

	uint64_t make_id ( const char* name, uint64_t extra ) {

		uint64_t hash = 0xCBF29CE484222325ull;

		for ( const char* at = name; *at; at++ )
			hash = ( hash ^ static_cast < uint8_t > ( *at ) ) * 0x100000001B3ull;

		hash ^= extra * 0x9E3779B97F4A7C15ull;

		return hash ? hash : 1;

	}

	float clamp ( float value, float low, float high ) {

		if ( high < low )
			return low;

		return value < low ? low : value > high ? high : value;

	}

	bool hovering ( ui_context& ui, const ui::rect& area ) {

		return ui::inside ( area, ui.input->x, ui.input->y ) && ( !ui.active || ui.active == ui.hot );

	}

	bool pressed_in ( ui_context& ui, uint64_t id, const ui::rect& area ) {

		if ( ui.input->pressed && ui::inside ( area, ui.input->x, ui.input->y ) ) {

			ui.active = id;
			ui.focus = id;

			return true;

		}

		return false;

	}

	bool key_hit ( ui_context& ui, uint32_t code ) {

		for ( uint32_t i = 0; i < ui.input->key_count; i++ )
			if ( ui.input->keys [ i ] == code )
				return true;

		return false;

	}

	static bool clickable ( ui_context& ui, uint64_t id, const ui::rect& area, bool& hover, bool& held ) {

		hover = ui::inside ( area, ui.input->x, ui.input->y ) && ( !ui.active || ui.active == id );

		if ( hover )
			ui.hot = id;

		if ( hover && ui.input->pressed )
			ui.active = id;

		held = ui.active == id && ui.input->down;

		bool clicked = ui.active == id && ui.input->released && hover;

		if ( clicked )
			ui.redraw = true;

		return clicked;

	}

	bool button ( ui_context& ui, uint64_t id, const ui::rect& area, const char* label, bool primary, bool enabled ) {

		bool hover = false;
		bool held = false;
		bool clicked = enabled && clickable ( ui, id, area, hover, held );

		if ( !enabled ) {

			ui::outline_round ( *ui.draw, area, LAYOUT_RADIUS, ui::rgb ( THEME_DIVIDER ) );
			ui::text ( *ui.draw, ui::FONT_UI, label, area, ui::rgb ( THEME_FAINT ), ui::ALIGN_CENTER );

			return false;

		}

		if ( primary ) {

			ui::color shade = ui::rgb ( THEME_ACCENT_DEEP );

			if ( held )
				shade = ui::mix ( shade, ui::rgb ( 0 ), 0.2f );
			else if ( hover )
				shade = ui::mix ( shade, ui::rgb ( 0xFFFFFF ), 0.08f );

			ui::fill_round ( *ui.draw, area, LAYOUT_RADIUS, shade );
			ui::text ( *ui.draw, ui::FONT_STRONG, label, area, ui::rgb ( 0xF2F5F6 ), ui::ALIGN_CENTER );

		} else {

			ui::fill_round ( *ui.draw, area, LAYOUT_RADIUS, ui::rgb ( held ? THEME_PRESSED : hover ? THEME_HOVER : THEME_RAISED ) );
			ui::outline_round ( *ui.draw, area, LAYOUT_RADIUS, ui::rgb ( THEME_BORDER ) );
			ui::text ( *ui.draw, ui::FONT_UI, label, area, ui::rgb ( THEME_TEXT ), ui::ALIGN_CENTER );

		}

		if ( hover )
			ui.cursor = CURSOR_HAND;

		return clicked;

	}

	bool tab ( ui_context& ui, uint64_t id, const ui::rect& area, const char* label, bool active ) {

		bool hover = false;
		bool held = false;
		bool clicked = clickable ( ui, id, area, hover, held );

		ui::text ( *ui.draw, active ? ui::FONT_STRONG : ui::FONT_UI, label, area, ui::rgb ( active ? THEME_TEXT : hover ? THEME_TEXT : THEME_DIM ), ui::ALIGN_CENTER );

		if ( active )
			ui::fill ( *ui.draw, { area.x + 8, area.y + area.h - 2, area.w - 16, 2 }, ui::rgb ( THEME_ACCENT ) );

		return clicked && !active;

	}

	bool segment ( ui_context& ui, uint64_t id, const ui::rect& area, const char* label, bool active, bool first, bool last ) {

		bool hover = false;
		bool held = false;
		bool clicked = clickable ( ui, id, area, hover, held );

		ui::fill ( *ui.draw, area, ui::rgb ( active ? THEME_SELECTED : hover ? THEME_HOVER : THEME_RAISED ) );
		ui::outline ( *ui.draw, area, ui::rgb ( THEME_BORDER ) );

		if ( active )
			ui::fill ( *ui.draw, { area.x + 1, area.y + area.h - 2, area.w - 2, 2 }, ui::rgb ( THEME_ACCENT ) );

		ui::text ( *ui.draw, ui::FONT_UI, label, area, ui::rgb ( active ? THEME_TEXT : THEME_DIM ), ui::ALIGN_CENTER );

		return clicked && !active;

	}

	void caption ( ui_context& ui, const ui::rect& area, const char* label, const char* right ) {

		base::text upper;

		for ( const char* at = label; *at; at++ )
			upper.add ( base::upper ( *at ) );

		ui::text ( *ui.draw, ui::FONT_CAPTION, upper.value ( ), upper.size, { area.x, area.y, area.w, area.h }, ui::rgb ( THEME_DIM ) );

		if ( right )
			ui::text ( *ui.draw, ui::FONT_SMALL, right, { area.x, area.y, area.w, area.h }, ui::rgb ( THEME_FAINT ), ui::ALIGN_RIGHT );

	}

	void key_value ( ui_context& ui, const ui::rect& area, const char* key, const char* value, ui::color shade ) {

		float split = area.w * 0.46f;

		ui::text ( *ui.draw, ui::FONT_UI, key, { area.x, area.y, split - 8, area.h }, ui::rgb ( THEME_DIM ) );
		ui::text ( *ui.draw, ui::FONT_UI, value, { area.x + split, area.y, area.w - split, area.h }, shade );

	}

	bool list_row ( ui_context& ui, uint64_t id, const ui::rect& area, bool selected ) {

		bool hover = false;
		bool held = false;
		bool clicked = clickable ( ui, id, area, hover, held );

		if ( selected ) {

			ui::fill ( *ui.draw, area, ui::rgb ( THEME_SELECTED ) );
			ui::fill ( *ui.draw, { area.x, area.y + 3, 2, area.h - 6 }, ui::rgb ( THEME_ACCENT ) );

		} else if ( hover )
			ui::fill ( *ui.draw, area, ui::rgb ( THEME_HOVER ) );

		return clicked;

	}

	float scrollbar ( ui_context& ui, uint64_t id, const ui::rect& track, float content, float view, float offset, bool vertical ) {

		float limit = content - view;

		if ( limit <= 0 )
			return 0;

		float length = vertical ? track.h : track.w;
		float thumb = clamp ( length * view / content, 24.0f, length );
		float travel = length - thumb;
		float at = travel * offset / limit;
		ui::rect knob = vertical ? ui::rect { track.x + 2, track.y + at, track.w - 4, thumb } : ui::rect { track.x + at, track.y + 2, thumb, track.h - 4 };
		bool hover = ui::inside ( track, ui.input->x, ui.input->y ) && ( !ui.active || ui.active == id );

		if ( hover )
			ui.hot = id;

		if ( hover && ui.input->pressed ) {

			ui.active = id;

			float mouse = vertical ? ui.input->y - track.y : ui.input->x - track.x;

			if ( mouse < at || mouse > at + thumb )
				offset = clamp ( ( mouse - thumb * 0.5f ) / travel * limit, 0, limit );

		}

		if ( ui.active == id && ui.input->pressed )
			ui.grab = ( vertical ? ui.input->y - track.y : ui.input->x - track.x ) - travel * offset / limit;

		if ( ui.active == id && ui.input->down ) {

			float mouse = ( vertical ? ui.input->y - track.y : ui.input->x - track.x ) - ui.grab;

			offset = clamp ( mouse / travel * limit, 0, limit );
			at = travel * offset / limit;
			knob = vertical ? ui::rect { track.x + 2, track.y + at, track.w - 4, thumb } : ui::rect { track.x + at, track.y + 2, thumb, track.h - 4 };

		}

		ui::fill_round ( *ui.draw, knob, 3.0f, ui::rgb ( ui.active == id ? 0x4A4A56 : hover ? 0x3C3C47 : 0x2E2E37 ) );

		return offset;

	}

	bool splitter ( ui_context& ui, uint64_t id, const ui::rect& area, bool vertical, float& value, float low, float high ) {

		bool hover = ui::inside ( area, ui.input->x, ui.input->y ) && ( !ui.active || ui.active == id );

		if ( hover ) {

			ui.hot = id;
			ui.cursor = vertical ? CURSOR_SIZE_WE : CURSOR_SIZE_NS;

		}

		if ( hover && ui.input->pressed )
			ui.active = id;

		if ( ui.active == id && ui.input->down ) {

			ui.cursor = vertical ? CURSOR_SIZE_WE : CURSOR_SIZE_NS;
			value = clamp ( vertical ? ui.input->x : ui.input->y, low, high );
			ui.redraw = true;

			return true;

		}

		return false;

	}

	void hover_text ( ui_context& ui, const char* value ) {

		ui.hover.clear ( );
		ui.hover.add ( value );
		ui.hover_set = true;

	}

	void table_glyph ( ui::renderer& draw, float x, float y, ui::color shade ) {

		ui::outline_round ( draw, { x, y, 13, 11 }, 2.0f, shade );
		ui::line ( draw, x + 1, y + 4, x + 12, y + 4, shade );
		ui::line ( draw, x + 5, y + 4, x + 5, y + 10, shade );

	}

	void index_glyph ( ui::renderer& draw, float x, float y, ui::color shade ) {

		ui::line ( draw, x + 1, y + 2, x + 12, y + 2, shade );
		ui::line ( draw, x + 1, y + 5.5f, x + 9, y + 5.5f, shade );
		ui::line ( draw, x + 1, y + 9, x + 6, y + 9, shade );

	}

}
