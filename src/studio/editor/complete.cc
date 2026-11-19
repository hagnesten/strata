#include "studio/editor/editor.hh"

#include <windows.h>

namespace studio {

	void add_completion ( completion_list& list, const char* word, size_t size, completion_kind kind, const char* detail ) {

		if ( list.count == COMPLETE_ROOM || !size || size >= COMPLETE_WORD )
			return;

		for ( uint32_t i = 0; i < list.count; i++ )
			if ( base::length ( list.items [ i ] ) == size && base::same_bytes ( list.items [ i ], word, size ) )
				return;

		base::copy ( list.items [ list.count ], word, size );
		list.items [ list.count ] [ size ] = 0;
		base::copy_text ( list.details [ list.count ], COMPLETE_WORD, detail ? detail : "" );
		list.kinds [ list.count ] = kind;
		list.count++;

	}

	void refresh_completion ( editor_state& editor ) {

		completion_list& list = editor.complete;
		const char* text = editor.text.value ( );
		size_t caret = editor.caret;
		size_t start = caret;

		list.open = false;

		if ( !editor.suggest || editor.caret != editor.anchor )
			return;

		while ( start && editor_word_char ( text [ start - 1 ] ) )
			start--;

		if ( start < caret && text [ start ] >= '0' && text [ start ] <= '9' )
			return;

		if ( caret && editor.shades && ( editor.shades [ caret - 1 ] == SHADE_STRING || editor.shades [ caret - 1 ] == SHADE_COMMENT ) )
			return;

		size_t qualifier_start = start;
		size_t qualifier_end = start;

		if ( start && text [ start - 1 ] == '.' ) {

			qualifier_end = start - 1;
			qualifier_start = qualifier_end;

			while ( qualifier_start && editor_word_char ( text [ qualifier_start - 1 ] ) )
				qualifier_start--;

		}

		if ( start == caret && qualifier_start == qualifier_end )
			return;

		list.count = 0;
		list.selected = 0;
		list.first = 0;
		list.start = start;
		editor.suggest ( editor.suggest_context, text + qualifier_start, qualifier_end - qualifier_start, text + start, caret - start, list );

		if ( list.count == 1 && base::length ( list.items [ 0 ] ) == caret - start && base::same_bytes ( list.items [ 0 ], text + start, caret - start ) )
			list.count = 0;

		list.open = list.count > 0;

	}

	static void accept ( editor_state& editor ) {

		completion_list& list = editor.complete;
		const char* word = list.items [ list.selected ];

		editor_replace ( editor, list.start, editor.caret, word, base::length ( word ) );
		list.open = false;

	}

	bool completion_key ( editor_state& editor, uint32_t key ) {

		completion_list& list = editor.complete;

		switch ( key ) {

			case VK_DOWN:
				list.selected = ( list.selected + 1 ) % list.count;
				break;

			case VK_UP:
				list.selected = list.selected ? list.selected - 1 : list.count - 1;
				break;

			case VK_TAB:
			case VK_RETURN:
				accept ( editor );

				return true;

			case VK_ESCAPE:
				list.open = false;

				return true;

			default:
				return false;

		}

		if ( list.selected < list.first )
			list.first = list.selected;

		if ( list.selected >= list.first + COMPLETE_SHOWN )
			list.first = list.selected + 1 - COMPLETE_SHOWN;

		return true;

	}

	static ui::rect popup_area ( const completion_list& list, const ui::rect& bounds ) {

		uint32_t shown = list.count < COMPLETE_SHOWN ? list.count : COMPLETE_SHOWN;
		float h = static_cast < float > ( shown ) * COMPLETE_ROW + 8;
		float x = clamp ( list.x, bounds.x + 4, bounds.x + bounds.w - COMPLETE_WIDTH - 4 );
		float y = list.y + h > bounds.y + bounds.h ? list.y - LAYOUT_LINE - h - 4 : list.y;

		return { x, y, COMPLETE_WIDTH, h };

	}

	bool completion_click ( ui_context& ui, editor_state& editor, const ui::rect& bounds ) {

		completion_list& list = editor.complete;
		input_state& input = *ui.input;

		if ( !list.open || !input.pressed )
			return false;

		ui::rect area = popup_area ( list, bounds );

		if ( !ui::inside ( area, input.x, input.y ) )
			return false;

		uint32_t row = static_cast < uint32_t > ( ( input.y - area.y - 4 ) / COMPLETE_ROW );

		if ( list.first + row < list.count ) {

			list.selected = list.first + row;
			accept ( editor );

		}

		input.pressed = false;
		ui.focus = editor.id;
		ui.redraw = true;

		return true;

	}

	void draw_completion ( ui_context& ui, editor_state& editor, const ui::rect& bounds ) {

		completion_list& list = editor.complete;

		if ( !list.open || !list.count || ui.focus != editor.id )
			return;

		ui::renderer& draw = *ui.draw;
		ui::rect area = popup_area ( list, bounds );
		static const char* const kinds [ 4 ] = { "table", "column", "keyword", "function" };

		ui::fill_round ( draw, { area.x + 2, area.y + 3, area.w, area.h }, LAYOUT_RADIUS, ui::rgb ( 0x000000, 0.35f ) );
		ui::fill_round ( draw, area, LAYOUT_RADIUS, ui::rgb ( THEME_RAISED ) );
		ui::outline_round ( draw, area, LAYOUT_RADIUS, ui::rgb ( THEME_BORDER ) );

		for ( uint32_t i = 0; i < COMPLETE_SHOWN && list.first + i < list.count; i++ ) {

			uint32_t at = list.first + i;
			ui::rect row = { area.x + 4, area.y + 4 + static_cast < float > ( i ) * COMPLETE_ROW, area.w - 8, COMPLETE_ROW };
			bool selected = at == list.selected;
			const char* detail = list.details [ at ] [ 0 ] ? list.details [ at ] : kinds [ list.kinds [ at ] ];

			if ( selected )
				ui::fill_round ( draw, row, 3, ui::rgb ( THEME_SELECTED ) );

			ui::text ( draw, ui::FONT_CODE, list.items [ at ], { row.x + 8, row.y, row.w - 110, row.h }, ui::rgb ( list.kinds [ at ] == COMPLETE_KEYWORD ? THEME_KEYWORD : list.kinds [ at ] == COMPLETE_FUNCTION ? THEME_FUNCTION : THEME_TEXT ) );
			ui::text ( draw, ui::FONT_SMALL, detail, { row.x, row.y, row.w - 8, row.h }, ui::rgb ( selected ? THEME_DIM : THEME_FAINT ), ui::ALIGN_RIGHT );

		}

	}

}