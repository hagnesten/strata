#include "sql/lexer/lexer.hh"

namespace sql {

	static bool name_start ( char c ) {

		return ( c >= 'a' && c <= 'z' ) || ( c >= 'A' && c <= 'Z' ) || c == '_' || static_cast < uint8_t > ( c ) >= 0x80;

	}

	static bool name_part ( char c ) {

		return name_start ( c ) || ( c >= '0' && c <= '9' ) || c == '$';

	}

	static bool digit ( char c ) {

		return c >= '0' && c <= '9';

	}

	static bool hex_digit ( char c ) {

		return digit ( c ) || ( c >= 'a' && c <= 'f' ) || ( c >= 'A' && c <= 'F' );

	}

	void start_lexer ( lexer& out, const char* source ) {

		out.source = source;
		out.at = source;
		out.line = 1;
		out.line_start = source;

	}

	static void skip_space ( lexer& item ) {

		for ( ;; ) {

			char c = *item.at;

			if ( c == '\n' ) {

				item.at++;
				item.line++;
				item.line_start = item.at;

			} else if ( c == ' ' || c == '\t' || c == '\r' )
				item.at++;
			else if ( c == '-' && item.at [ 1 ] == '-' ) {

				while ( *item.at && *item.at != '\n' )
					item.at++;

			} else if ( c == '/' && item.at [ 1 ] == '*' ) {

				item.at += 2;

				while ( *item.at && !( item.at [ 0 ] == '*' && item.at [ 1 ] == '/' ) ) {

					if ( *item.at == '\n' ) {

						item.line++;
						item.line_start = item.at + 1;

					}

					item.at++;

				}

				if ( *item.at )
					item.at += 2;

			} else
				return;

		}

	}

	token next_token ( lexer& item ) {

		skip_space ( item );

		token out = { };
		const char* start = item.at;
		char c = *start;

		out.start = start;
		out.line = item.line;
		out.column = static_cast < uint32_t > ( start - item.line_start ) + 1;

		if ( !c ) {

			out.kind = TOKEN_END;

			return out;

		}

		if ( ( c == 'x' || c == 'X' ) && start [ 1 ] == '\'' ) {

			const char* at = start + 2;

			while ( hex_digit ( *at ) )
				at++;

			out.kind = *at == '\'' && ( at - start - 2 ) % 2 == 0 ? TOKEN_BLOB : TOKEN_BAD;
			item.at = *at == '\'' ? at + 1 : at;
			out.size = static_cast < uint32_t > ( item.at - start );

			return out;

		}

		if ( name_start ( c ) ) {

			const char* at = start;

			while ( name_part ( *at ) )
				at++;

			out.kind = TOKEN_NAME;
			out.size = static_cast < uint32_t > ( at - start );
			item.at = at;

			return out;

		}

		if ( c == '"' || c == '`' || c == '[' ) {

			char close = c == '[' ? ']' : c;
			const char* at = start + 1;

			for ( ;; ) {

				if ( !*at ) {

					out.kind = TOKEN_BAD;
					item.at = at;
					out.size = static_cast < uint32_t > ( at - start );

					return out;

				}

				if ( *at == close ) {

					if ( close != ']' && at [ 1 ] == close ) {

						at += 2;

						continue;

					}

					break;

				}

				at++;

			}

			out.kind = TOKEN_NAME;
			item.at = at + 1;
			out.size = static_cast < uint32_t > ( item.at - start );

			return out;

		}

		if ( c == '\'' ) {

			const char* at = start + 1;

			for ( ;; ) {

				if ( !*at ) {

					out.kind = TOKEN_BAD;
					item.at = at;
					out.size = static_cast < uint32_t > ( at - start );

					return out;

				}

				if ( *at == '\n' ) {

					item.line++;
					item.line_start = at + 1;

				}

				if ( *at == '\'' ) {

					if ( at [ 1 ] == '\'' ) {

						at += 2;

						continue;

					}

					break;

				}

				at++;

			}

			out.kind = TOKEN_STRING;
			item.at = at + 1;
			out.size = static_cast < uint32_t > ( item.at - start );

			return out;

		}

		if ( digit ( c ) || ( c == '.' && digit ( start [ 1 ] ) ) ) {

			const char* at = start;
			bool real = false;

			if ( c == '0' && ( start [ 1 ] == 'x' || start [ 1 ] == 'X' ) && hex_digit ( start [ 2 ] ) ) {

				at += 2;

				while ( hex_digit ( *at ) )
					at++;

			} else {

				while ( digit ( *at ) )
					at++;

				if ( *at == '.' ) {

					real = true;
					at++;

					while ( digit ( *at ) )
						at++;

				}

				if ( ( *at == 'e' || *at == 'E' ) && ( digit ( at [ 1 ] ) || ( ( at [ 1 ] == '+' || at [ 1 ] == '-' ) && digit ( at [ 2 ] ) ) ) ) {

					real = true;
					at += 2;

					while ( digit ( *at ) )
						at++;

				}

			}

			out.kind = name_start ( *at ) ? TOKEN_BAD : real ? TOKEN_REAL : TOKEN_INTEGER;

			while ( name_part ( *at ) )
				at++;

			out.size = static_cast < uint32_t > ( at - start );
			item.at = at;

			return out;

		}

		if ( c == '?' || c == ':' || c == '@' || c == '$' ) {

			const char* at = start + 1;

			if ( c == '?' ) {

				while ( digit ( *at ) )
					at++;

			} else {

				while ( name_part ( *at ) )
					at++;

			}

			out.kind = c != '?' && at == start + 1 ? TOKEN_BAD : TOKEN_PARAMETER;
			out.size = static_cast < uint32_t > ( at - start );
			item.at = at;

			return out;

		}

		static const char* const pairs [ ] = { "<=", ">=", "<>", "!=", "==", "||", "<<", ">>" };

		for ( const char* pair : pairs ) {

			if ( c == pair [ 0 ] && start [ 1 ] == pair [ 1 ] ) {

				out.kind = TOKEN_SYMBOL;
				out.symbol [ 0 ] = pair [ 0 ];
				out.symbol [ 1 ] = pair [ 1 ];
				out.size = 2;
				item.at = start + 2;

				return out;

			}

		}

		static const char singles [ ] = "(),;.*+-/%<>=&|~";

		for ( const char* at = singles; *at; at++ ) {

			if ( c == *at ) {

				out.kind = TOKEN_SYMBOL;
				out.symbol [ 0 ] = c;
				out.size = 1;
				item.at = start + 1;

				return out;

			}

		}

		out.kind = TOKEN_BAD;
		out.size = 1;
		item.at = start + 1;

		return out;

	}

	bool token_is ( const token& item, const char* word ) {

		if ( item.kind != TOKEN_NAME || item.start [ 0 ] == '"' || item.start [ 0 ] == '`' || item.start [ 0 ] == '[' )
			return false;

		uint32_t i = 0;

		for ( ; i < item.size; i++ )
			if ( !word [ i ] || base::lower ( item.start [ i ] ) != base::lower ( word [ i ] ) )
				return false;

		return !word [ i ];

	}

	bool symbol_is ( const token& item, const char* symbol ) {

		return item.kind == TOKEN_SYMBOL && base::same ( item.symbol, symbol );

	}

}
