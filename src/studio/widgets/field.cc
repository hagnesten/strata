#include "studio/widgets/field.hh"
#include "base/clipboard/clipboard.hh"

#include <windows.h>

namespace studio {

	void set_field ( text_field& field, const char* value, size_t size ) {

		field.text.clear ( );
		field.text.add ( value, size );
		field.caret = field.text.size;
		field.anchor = 0;
		field.scroll = 0;

	}

	void set_field ( text_field& field, const char* value ) {

		set_field ( field, value, base::length ( value ) );

	}

	static uint64_t field_id ( text_field& field ) {

		if ( !field.id )
			field.id = make_id ( "field", reinterpret_cast < uint64_t > ( &field ) );

		return field.id;

	}

	void focus_field ( ui_context& ui, text_field& field ) {

		ui.focus = field_id ( field );
		ui.redraw = true;

	}

	static size_t previous_code ( const text_field& field, size_t at ) {

		if ( !at )
			return 0;

		at--;

		while ( at && ( static_cast < uint8_t > ( field.text.data [ at ] ) & 0xC0 ) == 0x80 )
			at--;

		return at;

	}

	static size_t next_code ( const text_field& field, size_t at ) {

		if ( at >= field.text.size )
			return field.text.size;

		base::next_code ( reinterpret_cast < const uint8_t* > ( field.text.value ( ) ), field.text.size, at );

		return at;

	}

	static void replace ( text_field& field, const char* value, size_t size ) {

		size_t from = field.caret < field.anchor ? field.caret : field.anchor;
		size_t to = field.caret < field.anchor ? field.anchor : field.caret;
		base::text made;

		made.add ( field.text.value ( ), from ).add ( value, size ).add ( field.text.value ( ) + to, field.text.size - to );
		field.text.clear ( );
		field.text.add ( made.value ( ), made.size );
		field.caret = from + size;
		field.anchor = field.caret;

	}

	static field_event handle_keys ( ui_context& ui, text_field& field ) {

		input_state& input = *ui.input;
		field_event event = FIELD_IDLE;

		for ( uint32_t k = 0; k < input.key_count; k++ ) {

			uint32_t key = input.keys [ k ];
			bool select = input.shift;

			switch ( key ) {

				case VK_RETURN:
					return FIELD_SUBMIT;

				case VK_ESCAPE:
					return FIELD_CANCEL;

				case VK_LEFT:
					field.caret = !select && field.caret != field.anchor ? ( field.caret < field.anchor ? field.caret : field.anchor ) : previous_code ( field, field.caret );

					if ( !select )
						field.anchor = field.caret;

					break;

				case VK_RIGHT:
					field.caret = !select && field.caret != field.anchor ? ( field.caret > field.anchor ? field.caret : field.anchor ) : next_code ( field, field.caret );

					if ( !select )
						field.anchor = field.caret;

					break;

				case VK_HOME:
					field.caret = 0;

					if ( !select )
						field.anchor = 0;

					break;

				case VK_END:
					field.caret = field.text.size;

					if ( !select )
						field.anchor = field.caret;

					break;

				case VK_BACK:
					if ( field.caret == field.anchor )
						field.anchor = previous_code ( field, field.caret );

					replace ( field, "", 0 );
					event = FIELD_CHANGED;
					break;

				case VK_DELETE:
					if ( field.caret == field.anchor )
						field.anchor = next_code ( field, field.caret );

					replace ( field, "", 0 );
					event = FIELD_CHANGED;
					break;

				case 'A':
					if ( input.ctrl ) {

						field.anchor = 0;
						field.caret = field.text.size;

					}

					break;

				case 'C':
					if ( input.ctrl ) {

						size_t from = field.caret < field.anchor ? field.caret : field.anchor;
						size_t to = field.caret < field.anchor ? field.anchor : field.caret;

						if ( from != to )
							base::copy_to_clipboard ( field.text.value ( ) + from, to - from );

					}

					break;

				case 'V':
					if ( input.ctrl ) {

						base::text pasted;
						base::text clean;

						if ( base::paste_from_clipboard ( pasted ) ) {

							for ( size_t i = 0; i < pasted.size; i++ )
								if ( pasted.data [ i ] != '\r' && pasted.data [ i ] != '\n' )
									clean.add ( pasted.data [ i ] == '\t' ? ' ' : pasted.data [ i ] );

							replace ( field, clean.value ( ), clean.size );
							event = FIELD_CHANGED;

						}

					}

					break;

				default:
					break;

			}

		}

		if ( !input.ctrl && !input.alt ) {

			for ( uint32_t c = 0; c < input.char_count; c++ ) {

				uint32_t code = input.chars [ c ];

				if ( code < 32 || code == 127 )
					continue;

				base::text typed;

				typed.code ( code );
				replace ( field, typed.value ( ), typed.size );
				event = FIELD_CHANGED;

			}

		}

		return event;

	}

	field_event text_box ( ui_context& ui, text_field& field, const ui::rect& area, const char* placeholder, bool code ) {

		ui::renderer& draw = *ui.draw;
		input_state& input = *ui.input;
		ui::font_kind font = code ? ui::FONT_CODE : ui::FONT_UI;
		bool hover = ui::inside ( area, input.x, input.y ) && ( !ui.active || ui.active == field.id );
		field_event event = FIELD_IDLE;

		field_id ( field );

		if ( hover )
			ui.cursor = CURSOR_TEXT;

		if ( hover && input.pressed ) {

			ui.focus = field.id;
			ui.active = field.id;
			ui.redraw = true;

			float x = input.x - area.x - 8 + field.scroll;
			size_t at = 0;

			while ( at < field.text.size ) {

				size_t next = next_code ( field, at );

				if ( ui::measure ( draw, font, field.text.value ( ), next ) > x )
					break;

				at = next;

			}

			field.caret = at;

			if ( !input.shift )
				field.anchor = at;

			if ( input.double_click ) {

				field.anchor = 0;
				field.caret = field.text.size;

			}

		}

		bool focused = ui.focus == field.id;

		if ( focused && ( input.key_count || input.char_count ) ) {

			event = handle_keys ( ui, field );
			ui.redraw = true;

		}

		float caret_x = ui::measure ( draw, font, field.text.value ( ), field.caret );
		float room = area.w - 16;

		if ( caret_x - field.scroll > room )
			field.scroll = caret_x - room;

		if ( caret_x < field.scroll )
			field.scroll = caret_x;

		ui::fill_round ( draw, area, LAYOUT_RADIUS, ui::rgb ( THEME_BACKGROUND ) );
		ui::outline_round ( draw, area, LAYOUT_RADIUS, ui::rgb ( focused ? THEME_ACCENT_DEEP : hover ? THEME_BORDER : THEME_DIVIDER ) );
		ui::push_clip ( draw, inset ( area, 2 ) );

		float origin = area.x + 8 - field.scroll;

		if ( focused && field.caret != field.anchor ) {

			size_t from = field.caret < field.anchor ? field.caret : field.anchor;
			size_t to = field.caret < field.anchor ? field.anchor : field.caret;
			float a = ui::measure ( draw, font, field.text.value ( ), from );
			float b = ui::measure ( draw, font, field.text.value ( ), to );

			ui::fill ( draw, { origin + a, area.y + 5, b - a, area.h - 10 }, ui::rgb ( THEME_ACCENT, 0.28f ) );

		}

		if ( field.text.size )
			ui::text ( draw, font, field.text.value ( ), field.text.size, { origin, area.y, 100000, area.h }, ui::rgb ( THEME_TEXT ) );
		else if ( placeholder )
			ui::text ( draw, font, placeholder, { area.x + 8, area.y, area.w - 16, area.h }, ui::rgb ( THEME_FAINT ) );

		if ( focused )
			ui::fill ( draw, { origin + caret_x - 0.5f, area.y + 6, 1.5f, area.h - 12 }, ui::rgb ( THEME_ACCENT_SOFT ) );

		ui::pop_clip ( draw );

		return event;

	}

}