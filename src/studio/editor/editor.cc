#include "studio/editor/editor.hh"
#include "base/clipboard/clipboard.hh"

#include <windows.h>

namespace studio {

	static const char* const keywords [ ] = {

		"select", "from", "where", "insert", "into", "values", "update", "set", "delete", "create", "table", "index", "unique",
		"primary", "key", "not", "null", "and", "or", "in", "is", "like", "glob", "between", "case", "when", "then", "else",
		"end", "as", "on", "join", "left", "inner", "cross", "outer", "using", "group", "by", "order", "having", "limit",
		"offset", "distinct", "all", "union", "intersect", "except", "exists", "default", "check", "references", "foreign",
		"cascade", "drop", "alter", "add", "column", "rename", "to", "begin", "commit", "rollback", "transaction", "explain",
		"query", "plan", "asc", "desc", "integer", "text", "real", "blob", "numeric", "int", "autoincrement", "conflict", "do",
		"nothing", "replace", "ignore", "abort", "if", "cast", "collate", "escape", "true", "false", "vacuum", "analyze",
		"restrict", "action", "no", "temp", "temporary", "varchar", "boolean", "date", "float", "double",

	};

	static bool word_char ( char value ) {

		return ( value >= 'a' && value <= 'z' ) || ( value >= 'A' && value <= 'Z' ) || ( value >= '0' && value <= '9' ) || value == '_' || static_cast < uint8_t > ( value ) >= 0x80;

	}

	static bool is_keyword ( const char* start, size_t size ) {

		if ( size > 16 )
			return false;

		char folded [ 17 ];

		for ( size_t i = 0; i < size; i++ )
			folded [ i ] = base::lower ( start [ i ] );

		folded [ size ] = 0;

		for ( const char* word : keywords )
			if ( base::same ( word, folded ) )
				return true;

		return false;

	}

	static void rebuild ( editor_state& editor ) {

		const char* text = editor.text.value ( );
		size_t size = editor.text.size;

		editor.line_count = 0;

		size_t start = 0;

		for ( size_t i = 0; i <= size; i++ ) {

			if ( i == size || text [ i ] == '\n' ) {

				if ( editor.line_count == editor.line_room ) {

					uint32_t grown = editor.line_room ? editor.line_room * 2 : 256;
					size_t* made = base::make < size_t > ( grown );

					if ( !made )
						return;

					if ( editor.lines ) {

						base::copy ( made, editor.lines, sizeof ( size_t ) * editor.line_count );
						base::release ( editor.lines );

					}

					editor.lines = made;
					editor.line_room = grown;

				}

				editor.lines [ editor.line_count++ ] = start;
				start = i + 1;

			}

		}

		if ( editor.shade_room < size + 1 ) {

			base::release ( editor.shades );
			editor.shade_room = size + 1024;
			editor.shades = base::make < uint8_t > ( editor.shade_room );

			if ( !editor.shades ) {

				editor.shade_room = 0;

				return;

			}

		}

		size_t at = 0;

		while ( at < size ) {

			char here = text [ at ];
			size_t begin = at;
			uint8_t shade = SHADE_PLAIN;

			if ( here == '-' && at + 1 < size && text [ at + 1 ] == '-' ) {

				while ( at < size && text [ at ] != '\n' )
					at++;

				shade = SHADE_COMMENT;

			} else if ( here == '/' && at + 1 < size && text [ at + 1 ] == '*' ) {

				at += 2;

				while ( at < size && !( text [ at ] == '*' && at + 1 < size && text [ at + 1 ] == '/' ) )
					at++;

				at = at + 2 < size ? at + 2 : size;
				shade = SHADE_COMMENT;

			} else if ( here == '\'' || here == '"' || here == '`' ) {

				at++;

				while ( at < size ) {

					if ( text [ at ] == here ) {

						if ( at + 1 < size && text [ at + 1 ] == here ) {

							at += 2;

							continue;

						}

						at++;

						break;

					}

					at++;

				}

				shade = here == '\'' ? SHADE_STRING : SHADE_PLAIN;

			} else if ( here >= '0' && here <= '9' ) {

				while ( at < size && ( word_char ( text [ at ] ) || text [ at ] == '.' ) )
					at++;

				shade = SHADE_NUMBER;

			} else if ( word_char ( here ) ) {

				while ( at < size && word_char ( text [ at ] ) )
					at++;

				size_t after = at;

				while ( after < size && ( text [ after ] == ' ' || text [ after ] == '\t' ) )
					after++;

				if ( is_keyword ( text + begin, at - begin ) )
					shade = SHADE_KEYWORD;
				else if ( after < size && text [ after ] == '(' )
					shade = SHADE_FUNCTION;

			} else {

				at++;
				shade = here == ' ' || here == '\n' || here == '\t' ? SHADE_PLAIN : SHADE_PUNCT;

			}

			base::fill ( editor.shades + begin, shade, at - begin );

		}

		editor.dirty = false;

	}

	void set_editor_text ( editor_state& editor, const char* value ) {

		editor.text.clear ( );
		editor.text.add ( value );
		editor.caret = editor.text.size;
		editor.anchor = editor.caret;
		editor.undo_count = 0;
		editor.redo_count = 0;
		editor.typing = false;
		editor.marked = -1;
		rebuild ( editor );

	}

	void editor_selection ( const editor_state& editor, size_t& from, size_t& to ) {

		from = editor.caret < editor.anchor ? editor.caret : editor.anchor;
		to = editor.caret < editor.anchor ? editor.anchor : editor.caret;

	}

	void editor_run_text ( const editor_state& editor, base::text& out, size_t& base_offset ) {

		size_t from = 0;
		size_t to = 0;

		editor_selection ( editor, from, to );
		out.clear ( );

		if ( from == to ) {

			out.add ( editor.text.value ( ), editor.text.size );
			base_offset = 0;

			return;

		}

		out.add ( editor.text.value ( ) + from, to - from );
		base_offset = from;

	}

	static uint32_t line_of ( const editor_state& editor, size_t at ) {

		uint32_t low = 0;
		uint32_t high = editor.line_count ? editor.line_count - 1 : 0;

		while ( low < high ) {

			uint32_t middle = ( low + high + 1 ) / 2;

			if ( editor.lines [ middle ] <= at )
				low = middle;
			else
				high = middle - 1;

		}

		return low;

	}

	static size_t line_end ( const editor_state& editor, uint32_t line ) {

		return line + 1 < editor.line_count ? editor.lines [ line + 1 ] - 1 : editor.text.size;

	}

	static uint32_t column_of ( const editor_state& editor, size_t at ) {

		uint32_t line = line_of ( editor, at );
		size_t start = editor.lines [ line ];

		return static_cast < uint32_t > ( base::count_codes ( reinterpret_cast < const uint8_t* > ( editor.text.value ( ) ) + start, at - start ) );

	}

	static size_t position_at ( const editor_state& editor, uint32_t line, uint32_t column ) {

		if ( line >= editor.line_count )
			return editor.text.size;

		size_t start = editor.lines [ line ];
		size_t end = line_end ( editor, line );
		size_t skipped = base::skip_codes ( reinterpret_cast < const uint8_t* > ( editor.text.value ( ) ) + start, end - start, column );

		return start + skipped;

	}

	void editor_place ( editor_state& editor, uint32_t line, uint32_t column ) {

		if ( editor.dirty )
			rebuild ( editor );

		editor.caret = position_at ( editor, line ? line - 1 : 0, column ? column - 1 : 0 );
		editor.anchor = editor.caret;
		editor.marked = static_cast < int64_t > ( line ? line - 1 : 0 );

	}

	static void remember ( editor_state& editor, bool typing ) {

		if ( typing && editor.typing && editor.typed_at == editor.caret )
			return;

		if ( editor.undo_count == EDITOR_UNDO ) {

			base::text spare;

			for ( uint32_t i = 1; i < EDITOR_UNDO; i++ ) {

				editor.undo [ i - 1 ].clear ( );
				editor.undo [ i - 1 ].add ( editor.undo [ i ].value ( ), editor.undo [ i ].size );
				editor.undo_caret [ i - 1 ] = editor.undo_caret [ i ];

			}

			editor.undo_count--;

		}

		editor.undo [ editor.undo_count ].clear ( );
		editor.undo [ editor.undo_count ].add ( editor.text.value ( ), editor.text.size );
		editor.undo_caret [ editor.undo_count ] = editor.caret;
		editor.undo_count++;
		editor.redo_count = 0;
		editor.typing = typing;

	}

	static void replace_range ( editor_state& editor, size_t from, size_t to, const char* value, size_t size ) {

		base::text& text = editor.text;
		size_t grown = text.size - ( to - from ) + size;

		if ( !text.reserve ( grown + 1 ) )
			return;

		base::move ( text.data + from + size, text.data + to, text.size - to );

		if ( size )
			base::copy ( text.data + from, value, size );

		text.size = grown;
		text.data [ grown ] = 0;
		editor.caret = from + size;
		editor.anchor = editor.caret;
		editor.typed_at = editor.caret;
		editor.dirty = true;
		editor.marked = -1;

	}

	static bool erase_selection ( editor_state& editor ) {

		size_t from = 0;
		size_t to = 0;

		editor_selection ( editor, from, to );

		if ( from == to )
			return false;

		replace_range ( editor, from, to, "", 0 );

		return true;

	}

	static void insert_text ( editor_state& editor, const char* value, size_t size, bool typing ) {

		remember ( editor, typing );

		size_t from = 0;
		size_t to = 0;

		editor_selection ( editor, from, to );
		replace_range ( editor, from, to, value, size );

	}

	static size_t previous_code ( const editor_state& editor, size_t at ) {

		if ( !at )
			return 0;

		at--;

		while ( at && ( static_cast < uint8_t > ( editor.text.data [ at ] ) & 0xC0 ) == 0x80 )
			at--;

		return at;

	}

	static size_t next_code ( const editor_state& editor, size_t at ) {

		if ( at >= editor.text.size )
			return editor.text.size;

		size_t moved = at;

		base::next_code ( reinterpret_cast < const uint8_t* > ( editor.text.value ( ) ), editor.text.size, moved );

		return moved;

	}

	static size_t word_left ( const editor_state& editor, size_t at ) {

		const char* text = editor.text.value ( );

		while ( at && !word_char ( text [ at - 1 ] ) )
			at--;

		while ( at && word_char ( text [ at - 1 ] ) )
			at--;

		return at;

	}

	static size_t word_right ( const editor_state& editor, size_t at ) {

		const char* text = editor.text.value ( );
		size_t size = editor.text.size;

		while ( at < size && !word_char ( text [ at ] ) )
			at++;

		while ( at < size && word_char ( text [ at ] ) )
			at++;

		return at;

	}

	static void move_to ( editor_state& editor, size_t at, bool select ) {

		editor.caret = at;

		if ( !select )
			editor.anchor = at;

		editor.typing = false;

	}

	static bool handle_keys ( ui_context& ui, editor_state& editor ) {

		input_state& input = *ui.input;
		bool run = false;

		for ( uint32_t k = 0; k < input.key_count; k++ ) {

			uint32_t key = input.keys [ k ];
			bool select = input.shift;
			bool vertical = key == VK_UP || key == VK_DOWN || key == VK_PRIOR || key == VK_NEXT;

			if ( editor.dirty )
				rebuild ( editor );

			if ( !vertical )
				editor.wanted = false;

			if ( ( key == VK_RETURN && input.ctrl ) || key == VK_F5 ) {

				run = true;

				continue;

			}

			switch ( key ) {

				case VK_LEFT:
					move_to ( editor, input.ctrl ? word_left ( editor, editor.caret ) : !select && editor.caret != editor.anchor ? ( editor.caret < editor.anchor ? editor.caret : editor.anchor ) : previous_code ( editor, editor.caret ), select );
					break;

				case VK_RIGHT:
					move_to ( editor, input.ctrl ? word_right ( editor, editor.caret ) : !select && editor.caret != editor.anchor ? ( editor.caret > editor.anchor ? editor.caret : editor.anchor ) : next_code ( editor, editor.caret ), select );
					break;

				case VK_UP:
				case VK_DOWN:
				case VK_PRIOR:
				case VK_NEXT: {

					uint32_t line = line_of ( editor, editor.caret );
					int32_t step = key == VK_UP ? -1 : key == VK_DOWN ? 1 : key == VK_PRIOR ? -20 : 20;
					int32_t target = static_cast < int32_t > ( line ) + step;

					if ( !editor.wanted ) {

						editor.wanted_x = static_cast < float > ( column_of ( editor, editor.caret ) );
						editor.wanted = true;

					}

					if ( target < 0 )
						move_to ( editor, 0, select );
					else if ( target >= static_cast < int32_t > ( editor.line_count ) )
						move_to ( editor, editor.text.size, select );
					else
						move_to ( editor, position_at ( editor, static_cast < uint32_t > ( target ), static_cast < uint32_t > ( editor.wanted_x ) ), select );

					break;

				}

				case VK_HOME: {

					if ( input.ctrl ) {

						move_to ( editor, 0, select );

						break;

					}

					uint32_t line = line_of ( editor, editor.caret );
					size_t start = editor.lines [ line ];
					size_t first = start;

					while ( first < editor.text.size && ( editor.text.data [ first ] == ' ' || editor.text.data [ first ] == '\t' ) )
						first++;

					move_to ( editor, editor.caret == first ? start : first, select );

					break;

				}

				case VK_END:
					move_to ( editor, input.ctrl ? editor.text.size : line_end ( editor, line_of ( editor, editor.caret ) ), select );
					break;

				case VK_BACK:
					remember ( editor, false );

					if ( !erase_selection ( editor ) && editor.caret ) {

						size_t from = input.ctrl ? word_left ( editor, editor.caret ) : previous_code ( editor, editor.caret );

						replace_range ( editor, from, editor.caret, "", 0 );

					}

					break;

				case VK_DELETE:
					remember ( editor, false );

					if ( !erase_selection ( editor ) && editor.caret < editor.text.size ) {

						size_t to = input.ctrl ? word_right ( editor, editor.caret ) : next_code ( editor, editor.caret );

						replace_range ( editor, editor.caret, to, "", 0 );

					}

					break;

				case VK_RETURN: {

					uint32_t line = line_of ( editor, editor.caret );
					size_t start = editor.lines [ line ];
					base::text indent;

					indent.add ( '\n' );

					while ( start < editor.caret && editor.text.data [ start ] == ' ' )
						indent.add ( editor.text.data [ start++ ] );

					insert_text ( editor, indent.value ( ), indent.size, false );

					break;

				}

				case VK_TAB:
					insert_text ( editor, "    ", EDITOR_TAB, false );
					break;

				case 'A':
					if ( input.ctrl ) {

						editor.anchor = 0;
						editor.caret = editor.text.size;

					}

					break;

				case 'C':
				case 'X':
					if ( input.ctrl ) {

						size_t from = 0;
						size_t to = 0;

						editor_selection ( editor, from, to );

						if ( from == to ) {

							uint32_t line = line_of ( editor, editor.caret );

							from = editor.lines [ line ];
							to = line + 1 < editor.line_count ? editor.lines [ line + 1 ] : editor.text.size;

						}

						base::copy_to_clipboard ( editor.text.value ( ) + from, to - from );

						if ( key == 'X' ) {

							remember ( editor, false );
							replace_range ( editor, from, to, "", 0 );

						}

					}

					break;

				case 'V':
					if ( input.ctrl ) {

						base::text pasted;
						base::text clean;

						if ( base::paste_from_clipboard ( pasted ) ) {

							for ( size_t i = 0; i < pasted.size; i++ ) {

								if ( pasted.data [ i ] == '\r' )
									continue;

								if ( pasted.data [ i ] == '\t' )
									clean.repeat ( ' ', EDITOR_TAB );
								else
									clean.add ( pasted.data [ i ] );

							}

							insert_text ( editor, clean.value ( ), clean.size, false );

						}

					}

					break;

				case 'Z':
				case 'Y':
					if ( input.ctrl ) {

						bool redo = key == 'Y' || input.shift;

						if ( !redo && editor.undo_count ) {

							uint32_t at = editor.undo_count - 1;
							base::text current;

							current.add ( editor.text.value ( ), editor.text.size );

							size_t caret = editor.undo_caret [ at ];

							editor.text.clear ( );
							editor.text.add ( editor.undo [ at ].value ( ), editor.undo [ at ].size );
							editor.undo [ at ].clear ( );
							editor.undo [ at ].add ( current.value ( ), current.size );
							editor.undo_caret [ at ] = editor.caret;
							editor.undo_count--;
							editor.redo_count++;
							editor.caret = caret < editor.text.size ? caret : editor.text.size;
							editor.anchor = editor.caret;
							editor.dirty = true;
							editor.typing = false;

						} else if ( redo && editor.redo_count ) {

							uint32_t at = editor.undo_count;
							base::text current;

							current.add ( editor.text.value ( ), editor.text.size );

							size_t caret = editor.undo_caret [ at ];

							editor.text.clear ( );
							editor.text.add ( editor.undo [ at ].value ( ), editor.undo [ at ].size );
							editor.undo [ at ].clear ( );
							editor.undo [ at ].add ( current.value ( ), current.size );
							editor.undo_caret [ at ] = editor.caret;
							editor.undo_count++;
							editor.redo_count--;
							editor.caret = caret < editor.text.size ? caret : editor.text.size;
							editor.anchor = editor.caret;
							editor.dirty = true;
							editor.typing = false;

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
				insert_text ( editor, typed.value ( ), typed.size, code != ' ' );

			}

		}

		return run;

	}

	static uint32_t shade_color ( uint8_t shade ) {

		switch ( shade ) {

			case SHADE_KEYWORD: return THEME_KEYWORD;
			case SHADE_STRING: return THEME_STRING;
			case SHADE_NUMBER: return THEME_NUMBER;
			case SHADE_COMMENT: return THEME_COMMENT;
			case SHADE_FUNCTION: return THEME_FUNCTION;
			case SHADE_PUNCT: return THEME_DIM;
			default: return THEME_TEXT;

		}

	}

	bool draw_editor ( ui_context& ui, editor_state& editor, const ui::rect& area ) {

		ui::renderer& draw = *ui.draw;
		input_state& input = *ui.input;
		float advance = draw.code_advance;
		bool run = false;
		ui::rect body = { area.x + LAYOUT_GUTTER, area.y, area.w - LAYOUT_GUTTER - LAYOUT_SCROLLBAR, area.h };
		bool hover = ui::inside ( area, input.x, input.y ) && ( !ui.active || ui.active == editor.id );

		if ( editor.dirty || !editor.line_count )
			rebuild ( editor );

		if ( hover && ui::inside ( body, input.x, input.y ) )
			ui.cursor = CURSOR_TEXT;

		if ( hover && input.wheel != 0 )
			editor.scroll_y -= input.wheel * LAYOUT_LINE * 3;

		auto locate = [ & ] ( ) {

			float row = ( input.y - area.y - 8 + editor.scroll_y ) / LAYOUT_LINE;
			float column = ( input.x - body.x - 6 + editor.scroll_x ) / advance + 0.5f;
			int32_t line = static_cast < int32_t > ( row < 0 ? 0 : row );
			uint32_t at_line = line >= static_cast < int32_t > ( editor.line_count ) ? editor.line_count - 1 : static_cast < uint32_t > ( line );

			return position_at ( editor, at_line, column < 0 ? 0 : static_cast < uint32_t > ( column ) );

		};

		if ( hover && input.pressed && ui::inside ( area, input.x, input.y ) ) {

			ui.active = editor.id;
			ui.focus = editor.id;

			size_t at = locate ( );

			if ( input.double_click ) {

				editor.anchor = word_left ( editor, at < editor.text.size && word_char ( editor.text.data [ at ] ) ? at + 1 : at );
				editor.caret = word_right ( editor, editor.anchor );

			} else
				move_to ( editor, at, input.shift );

			editor.dragging = !input.double_click;
			editor.wanted = false;
			ui.redraw = true;

		}

		if ( editor.dragging && ui.active == editor.id && input.down ) {

			editor.caret = locate ( );
			ui.cursor = CURSOR_TEXT;

		}

		if ( !input.down )
			editor.dragging = false;

		bool focused = ui.focus == editor.id;

		if ( focused && ( input.key_count || input.char_count ) ) {

			size_t before = editor.caret;

			run = handle_keys ( ui, editor );

			if ( editor.dirty )
				rebuild ( editor );

			if ( editor.caret != before || input.char_count ) {

				uint32_t line = line_of ( editor, editor.caret );
				float top = static_cast < float > ( line ) * LAYOUT_LINE;
				float view = area.h - 16;
				float x = static_cast < float > ( column_of ( editor, editor.caret ) ) * advance;

				if ( top < editor.scroll_y )
					editor.scroll_y = top;

				if ( top + LAYOUT_LINE > editor.scroll_y + view )
					editor.scroll_y = top + LAYOUT_LINE - view;

				if ( x < editor.scroll_x )
					editor.scroll_x = x;

				if ( x > editor.scroll_x + body.w - 24 )
					editor.scroll_x = x - body.w + 24;

			}

			ui.redraw = true;

		}

		float content = static_cast < float > ( editor.line_count ) * LAYOUT_LINE + 16;

		editor.scroll_y = clamp ( editor.scroll_y, 0, content - area.h );
		editor.scroll_x = clamp ( editor.scroll_x, 0, 100000 );

		ui::fill ( draw, area, ui::rgb ( THEME_BACKGROUND ) );
		ui::fill ( draw, { area.x, area.y, LAYOUT_GUTTER, area.h }, ui::rgb ( THEME_PANEL ) );
		ui::line ( draw, area.x + LAYOUT_GUTTER - 0.5f, area.y, area.x + LAYOUT_GUTTER - 0.5f, area.y + area.h, ui::rgb ( THEME_DIVIDER ) );

		size_t from = 0;
		size_t to = 0;

		editor_selection ( editor, from, to );

		uint32_t caret_line = line_of ( editor, editor.caret );
		uint32_t first = static_cast < uint32_t > ( editor.scroll_y / LAYOUT_LINE );
		uint32_t visible = static_cast < uint32_t > ( area.h / LAYOUT_LINE ) + 2;
		const char* text = editor.text.value ( );

		ui::push_clip ( draw, area );

		for ( uint32_t i = 0; i < visible && first + i < editor.line_count; i++ ) {

			uint32_t line = first + i;
			float y = area.y + 8 + static_cast < float > ( line ) * LAYOUT_LINE - editor.scroll_y;
			size_t start = editor.lines [ line ];
			size_t end = line_end ( editor, line );
			base::text number;

			if ( focused && line == caret_line && from == to )
				ui::fill ( draw, { body.x, y, body.w, LAYOUT_LINE }, ui::rgb ( 0x161A1D ) );

			if ( editor.marked == static_cast < int64_t > ( line ) )
				ui::fill ( draw, { body.x, y, body.w, LAYOUT_LINE }, ui::rgb ( THEME_ERROR, 0.12f ) );

			number.number ( line + 1 );
			ui::text ( draw, ui::FONT_CODE_SMALL, number.value ( ), number.size, { area.x, y, LAYOUT_GUTTER - 12, LAYOUT_LINE }, ui::rgb ( line == caret_line ? THEME_DIM : THEME_FAINT ), ui::ALIGN_RIGHT );

			ui::push_clip ( draw, body );

			if ( from != to && from <= end && to >= start ) {

				size_t a = from > start ? from : start;
				size_t b = to < end ? to : end;
				float x1 = static_cast < float > ( base::count_codes ( reinterpret_cast < const uint8_t* > ( text ) + start, a - start ) ) * advance;
				float x2 = static_cast < float > ( base::count_codes ( reinterpret_cast < const uint8_t* > ( text ) + start, b - start ) ) * advance;

				if ( to > end )
					x2 += advance * 0.5f;

				ui::fill ( draw, { body.x + 6 + x1 - editor.scroll_x, y, x2 - x1, LAYOUT_LINE }, ui::rgb ( THEME_ACCENT, 0.28f ) );

			}

			size_t at = start;
			uint32_t column = 0;

			while ( at < end ) {

				uint8_t shade = editor.shades [ at ];
				size_t run_end = at;

				while ( run_end < end && editor.shades [ run_end ] == shade )
					run_end++;

				uint32_t codes = static_cast < uint32_t > ( base::count_codes ( reinterpret_cast < const uint8_t* > ( text ) + at, run_end - at ) );
				float x = body.x + 6 + static_cast < float > ( column ) * advance - editor.scroll_x;
				float width = static_cast < float > ( codes ) * advance + advance;

				if ( x < body.x + body.w && x + width > body.x )
					ui::text ( draw, ui::FONT_CODE, text + at, run_end - at, { x, y, width + 2, LAYOUT_LINE }, ui::rgb ( shade_color ( shade ) ) );

				column += codes;
				at = run_end;

			}

			if ( focused && line == caret_line ) {

				float x = body.x + 6 + static_cast < float > ( column_of ( editor, editor.caret ) ) * advance - editor.scroll_x;

				ui::fill ( draw, { x - 0.5f, y + 2, 2, LAYOUT_LINE - 4 }, ui::rgb ( THEME_ACCENT_SOFT ) );

			}

			ui::pop_clip ( draw );

		}

		ui::pop_clip ( draw );

		editor.scroll_y = scrollbar ( ui, editor.id ^ 0x91, { area.x + area.w - LAYOUT_SCROLLBAR, area.y, LAYOUT_SCROLLBAR, area.h }, content, area.h, editor.scroll_y, true );

		return run;

	}

}
