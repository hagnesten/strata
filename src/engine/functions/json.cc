#include "engine/run/run.hh"

#define JSON_DEPTH 1000
#define JSON_SUBTYPE 1

namespace engine {

	static void skip_space ( const uint8_t* text, size_t size, size_t& at ) {

		while ( at < size && ( text [ at ] == ' ' || text [ at ] == '\t' || text [ at ] == '\n' || text [ at ] == '\r' ) )
			at++;

	}

	static bool hex_value ( uint8_t c, uint32_t& out ) {

		if ( c >= '0' && c <= '9' )
			out = c - '0';
		else if ( c >= 'a' && c <= 'f' )
			out = c - 'a' + 10;
		else if ( c >= 'A' && c <= 'F' )
			out = c - 'A' + 10;
		else
			return false;

		return true;

	}

	static bool skip_string ( const uint8_t* text, size_t size, size_t& at ) {

		if ( at >= size || text [ at ] != '"' )
			return false;

		at++;

		while ( at < size ) {

			uint8_t c = text [ at ];

			if ( c == '"' ) {

				at++;

				return true;

			}

			if ( c == '\\' ) {

				at++;

				if ( at >= size )
					return false;

				uint8_t escape = text [ at++ ];

				if ( escape == 'u' ) {

					for ( uint32_t i = 0; i < 4; i++ ) {

						uint32_t digit = 0;

						if ( at >= size || !hex_value ( text [ at++ ], digit ) )
							return false;

					}

				} else if ( escape != '"' && escape != '\\' && escape != '/' && escape != 'b' && escape != 'f' && escape != 'n' && escape != 'r' && escape != 't' )
					return false;

				continue;

			}

			if ( c < 0x20 )
				return false;

			at++;

		}

		return false;

	}

	static bool skip_literal ( const uint8_t* text, size_t size, size_t& at, const char* word ) {

		for ( const char* w = word; *w; w++ )
			if ( at >= size || text [ at++ ] != static_cast < uint8_t > ( *w ) )
				return false;

		return true;

	}

	static bool skip_value ( const uint8_t* text, size_t size, size_t& at, uint32_t depth ) {

		if ( depth > JSON_DEPTH )
			return false;

		skip_space ( text, size, at );

		if ( at >= size )
			return false;

		uint8_t c = text [ at ];

		if ( c == '{' || c == '[' ) {

			bool object = c == '{';
			uint8_t close = object ? '}' : ']';

			at++;
			skip_space ( text, size, at );

			if ( at < size && text [ at ] == close ) {

				at++;

				return true;

			}

			for ( ;; ) {

				skip_space ( text, size, at );

				if ( object ) {

					if ( !skip_string ( text, size, at ) )
						return false;

					skip_space ( text, size, at );

					if ( at >= size || text [ at++ ] != ':' )
						return false;

				}

				if ( !skip_value ( text, size, at, depth + 1 ) )
					return false;

				skip_space ( text, size, at );

				if ( at >= size )
					return false;

				if ( text [ at ] == ',' ) {

					at++;

					continue;

				}

				if ( text [ at ] == close ) {

					at++;

					return true;

				}

				return false;

			}

		}

		if ( c == '"' )
			return skip_string ( text, size, at );

		if ( c == 't' )
			return skip_literal ( text, size, at, "true" );

		if ( c == 'f' )
			return skip_literal ( text, size, at, "false" );

		if ( c == 'n' )
			return skip_literal ( text, size, at, "null" );

		if ( c == '-' )
			at++;

		size_t digits = at;

		while ( at < size && text [ at ] >= '0' && text [ at ] <= '9' )
			at++;

		if ( at == digits )
			return false;

		if ( at < size && text [ at ] == '.' ) {

			at++;

			size_t fraction = at;

			while ( at < size && text [ at ] >= '0' && text [ at ] <= '9' )
				at++;

			if ( at == fraction )
				return false;

		}

		if ( at < size && ( text [ at ] == 'e' || text [ at ] == 'E' ) ) {

			at++;

			if ( at < size && ( text [ at ] == '+' || text [ at ] == '-' ) )
				at++;

			size_t exponent = at;

			while ( at < size && text [ at ] >= '0' && text [ at ] <= '9' )
				at++;

			if ( at == exponent )
				return false;

		}

		return true;

	}

	static bool valid_json ( const uint8_t* text, size_t size ) {

		size_t at = 0;

		if ( !skip_value ( text, size, at, 0 ) )
			return false;

		skip_space ( text, size, at );

		return at == size;

	}

	static bool write_canonical ( const uint8_t* text, size_t size, size_t& at, base::text& out, uint32_t depth ) {

		skip_space ( text, size, at );

		if ( at >= size || depth > 200 )
			return false;

		if ( text [ at ] == '[' ) {

			at++;
			out.add ( '[' );
			skip_space ( text, size, at );

			bool first = true;

			while ( at < size && text [ at ] != ']' ) {

				if ( !first )
					out.add ( ", " );

				if ( !write_canonical ( text, size, at, out, depth + 1 ) )
					return false;

				skip_space ( text, size, at );
				first = false;

				if ( at < size && text [ at ] == ',' )
					at++;
				else if ( at >= size || text [ at ] != ']' )
					return false;

			}

			if ( at >= size )
				return false;

			at++;
			out.add ( ']' );

			return true;

		}

		if ( text [ at ] != '{' ) {

			size_t start = at;

			if ( !skip_value ( text, size, at, depth ) )
				return false;

			out.add ( reinterpret_cast < const char* > ( text + start ), at - start );

			return true;

		}

		uint32_t room = 0;
		uint32_t count = 0;
		size_t* spans = nullptr;
		uint32_t* order = nullptr;
		bool fine = true;

		at++;
		skip_space ( text, size, at );

		while ( fine && at < size && text [ at ] != '}' ) {

			size_t key = at;

			if ( text [ at ] != '"' || !skip_string ( text, size, at ) ) {

				fine = false;

				break;

			}

			size_t key_end = at;

			skip_space ( text, size, at );

			if ( at >= size || text [ at++ ] != ':' ) {

				fine = false;

				break;

			}

			skip_space ( text, size, at );

			size_t value = at;

			if ( !skip_value ( text, size, at, depth + 1 ) ) {

				fine = false;

				break;

			}

			size_t value_end = at;
			uint32_t slot = count;

			for ( uint32_t k = 0; k < count; k++ )
				if ( spans [ k * 4 + 1 ] - spans [ k * 4 ] == key_end - key && base::same_bytes ( text + spans [ k * 4 ], text + key, key_end - key ) )
					slot = k;

			if ( slot == count ) {

				if ( count == room ) {

					uint32_t grown = room ? room * 2 : 16;
					size_t* made = base::make < size_t > ( static_cast < size_t > ( grown ) * 4 );

					if ( !made ) {

						fine = false;

						break;

					}

					if ( spans )
						base::copy ( made, spans, sizeof ( size_t ) * count * 4 );

					base::release ( spans );
					spans = made;
					room = grown;

				}

				count++;

			}

			spans [ slot * 4 ] = key;
			spans [ slot * 4 + 1 ] = key_end;
			spans [ slot * 4 + 2 ] = value;
			spans [ slot * 4 + 3 ] = value_end;
			skip_space ( text, size, at );

			if ( at < size && text [ at ] == ',' ) {

				at++;
				skip_space ( text, size, at );

			} else if ( at >= size || text [ at ] != '}' )
				fine = false;

		}

		order = fine && count ? base::make < uint32_t > ( count ) : nullptr;

		if ( !fine || at >= size || ( count && !order ) ) {

			base::release ( spans );
			base::release ( order );

			return false;

		}

		at++;

		for ( uint32_t k = 0; k < count; k++ )
			order [ k ] = k;

		for ( uint32_t i = 1; i < count; i++ ) {

			uint32_t moving = order [ i ];
			size_t length = spans [ moving * 4 + 1 ] - spans [ moving * 4 ];
			uint32_t j = i;

			while ( j > 0 ) {

				uint32_t other = order [ j - 1 ];
				size_t other_length = spans [ other * 4 + 1 ] - spans [ other * 4 ];
				size_t shared = length < other_length ? length : other_length;
				int32_t order_bytes = __builtin_memcmp ( text + spans [ other * 4 ], text + spans [ moving * 4 ], shared );

				if ( other_length < length || ( other_length == length && order_bytes <= 0 ) )
					break;

				order [ j ] = other;
				j--;

			}

			order [ j ] = moving;

		}

		out.add ( '{' );

		for ( uint32_t i = 0; i < count && fine; i++ ) {

			uint32_t k = order [ i ];
			size_t inner = spans [ k * 4 + 2 ];

			if ( i )
				out.add ( ", " );

			out.add ( reinterpret_cast < const char* > ( text + spans [ k * 4 ] ), spans [ k * 4 + 1 ] - spans [ k * 4 ] ).add ( ": " );
			fine = write_canonical ( text, spans [ k * 4 + 3 ], inner, out, depth + 1 );

		}

		out.add ( '}' );
		base::release ( spans );
		base::release ( order );

		return fine;

	}

	bool canonical_json ( const uint8_t* text, size_t size, base::text& out ) {

		size_t at = 0;

		if ( !write_canonical ( text, size, at, out, 0 ) )
			return false;

		skip_space ( text, size, at );

		return at == size;

	}

	static void decode_string ( const uint8_t* text, size_t start, size_t end, base::text& out ) {

		for ( size_t at = start + 1; at + 1 < end; ) {

			uint8_t c = text [ at++ ];

			if ( c != '\\' ) {

				out.add ( static_cast < char > ( c ) );

				continue;

			}

			uint8_t escape = text [ at++ ];

			switch ( escape ) {

				case 'b': out.add ( '\b' ); break;
				case 'f': out.add ( '\f' ); break;
				case 'n': out.add ( '\n' ); break;
				case 'r': out.add ( '\r' ); break;
				case 't': out.add ( '\t' ); break;
				case 'u': {

					uint32_t code = 0;

					for ( uint32_t i = 0; i < 4; i++ ) {

						uint32_t digit = 0;

						hex_value ( text [ at++ ], digit );
						code = code << 4 | digit;

					}

					if ( code >= 0xD800 && code < 0xDC00 && at + 5 < end && text [ at ] == '\\' && text [ at + 1 ] == 'u' ) {

						uint32_t low = 0;

						for ( uint32_t i = 0; i < 4; i++ ) {

							uint32_t digit = 0;

							hex_value ( text [ at + 2 + i ], digit );
							low = low << 4 | digit;

						}

						if ( low >= 0xDC00 && low < 0xE000 ) {

							code = 0x10000 + ( ( code - 0xD800 ) << 10 ) + ( low - 0xDC00 );
							at += 6;

						}

					}

					out.code ( code );
					break;

				}

				default: out.add ( static_cast < char > ( escape ) ); break;

			}

		}

	}

	static void minify ( const uint8_t* text, size_t start, size_t end, base::text& out ) {

		bool quoted = false;

		for ( size_t at = start; at < end; at++ ) {

			uint8_t c = text [ at ];

			if ( quoted ) {

				out.add ( static_cast < char > ( c ) );

				if ( c == '\\' && at + 1 < end )
					out.add ( static_cast < char > ( text [ ++at ] ) );
				else if ( c == '"' )
					quoted = false;

				continue;

			}

			if ( c == ' ' || c == '\t' || c == '\n' || c == '\r' )
				continue;

			if ( c == '"' )
				quoted = true;

			out.add ( static_cast < char > ( c ) );

		}

	}

	static bool keep_text ( run_context& ctx, const base::text& made, bool json, storage::value& out ) {

		char* kept = ctx.scratch->keep ( made.value ( ), made.size );

		if ( !kept )
			return fail_run ( ctx, "out of memory" );

		out = storage::text_value ( kept, made.size );
		out.subtype = json ? JSON_SUBTYPE : 0;

		return true;

	}

	static bool walk ( run_context& ctx, const uint8_t* text, size_t size, const uint8_t* path, size_t path_size, size_t& start, size_t& end, bool& found ) {

		found = false;

		if ( !path_size || path [ 0 ] != '$' )
			return fail_run ( ctx, "a JSON path must start with $" );

		start = 0;
		skip_space ( text, size, start );
		end = start;

		if ( !skip_value ( text, size, end, 0 ) )
			return fail_run ( ctx, "malformed JSON" );

		size_t at = 1;

		while ( at < path_size ) {

			if ( path [ at ] == '.' ) {

				at++;

				base::text key;

				if ( at < path_size && path [ at ] == '"' ) {

					at++;

					while ( at < path_size && path [ at ] != '"' )
						key.add ( static_cast < char > ( path [ at++ ] ) );

					if ( at >= path_size )
						return fail_run ( ctx, "malformed JSON path" );

					at++;

				} else {

					while ( at < path_size && path [ at ] != '.' && path [ at ] != '[' )
						key.add ( static_cast < char > ( path [ at++ ] ) );

				}

				if ( text [ start ] != '{' )
					return true;

				size_t cursor = start + 1;
				bool hit = false;

				skip_space ( text, size, cursor );

				while ( cursor < end && text [ cursor ] == '"' ) {

					size_t key_start = cursor;

					skip_string ( text, size, cursor );

					base::text name;

					decode_string ( text, key_start, cursor, name );
					skip_space ( text, size, cursor );
					cursor++;
					skip_space ( text, size, cursor );

					size_t value_start = cursor;

					skip_value ( text, size, cursor, 0 );

					if ( name.size == key.size && base::same_bytes ( name.value ( ), key.value ( ), key.size ) ) {

						start = value_start;
						end = cursor;
						hit = true;

						break;

					}

					skip_space ( text, size, cursor );

					if ( cursor < end && text [ cursor ] == ',' ) {

						cursor++;
						skip_space ( text, size, cursor );

					}

				}

				if ( !hit )
					return true;

				continue;

			}

			if ( path [ at ] == '[' ) {

				at++;

				bool from_end = false;
				int64_t index = 0;

				if ( at < path_size && path [ at ] == '#' ) {

					from_end = true;
					at++;

					if ( at < path_size && path [ at ] == '-' )
						at++;

				}

				while ( at < path_size && path [ at ] >= '0' && path [ at ] <= '9' )
					index = index * 10 + ( path [ at++ ] - '0' );

				if ( at >= path_size || path [ at ] != ']' )
					return fail_run ( ctx, "malformed JSON path" );

				at++;

				if ( text [ start ] != '[' )
					return true;

				int64_t count = 0;
				size_t cursor = start + 1;

				skip_space ( text, size, cursor );

				if ( from_end ) {

					size_t counting = cursor;

					while ( counting < end && text [ counting ] != ']' ) {

						skip_value ( text, size, counting, 0 );
						count++;
						skip_space ( text, size, counting );

						if ( counting < end && text [ counting ] == ',' )
							counting++;

						skip_space ( text, size, counting );

					}

					index = count - index;

					if ( index < 0 )
						return true;

				}

				int64_t position = 0;
				bool hit = false;

				while ( cursor < end && text [ cursor ] != ']' ) {

					size_t value_start = cursor;

					skip_value ( text, size, cursor, 0 );

					if ( position == index ) {

						start = value_start;
						end = cursor;
						hit = true;

						break;

					}

					position++;
					skip_space ( text, size, cursor );

					if ( cursor < end && text [ cursor ] == ',' )
						cursor++;

					skip_space ( text, size, cursor );

				}

				if ( !hit )
					return true;

				continue;

			}

			return fail_run ( ctx, "malformed JSON path" );

		}

		found = true;

		return true;

	}

	static bool to_value ( run_context& ctx, const uint8_t* text, size_t start, size_t end, storage::value& out ) {

		uint8_t c = text [ start ];

		if ( c == '"' ) {

			base::text made;

			decode_string ( text, start, end, made );

			return keep_text ( ctx, made, false, out );

		}

		if ( c == 't' || c == 'f' ) {

			out = storage::integer_value ( c == 't' ? 1 : 0 );

			return true;

		}

		if ( c == 'n' ) {

			out = storage::null_value ( );

			return true;

		}

		if ( c == '{' || c == '[' ) {

			base::text made;

			minify ( text, start, end, made );

			return keep_text ( ctx, made, true, out );

		}

		if ( !read_number_text ( text + start, end - start, out, true ) )
			out = storage::null_value ( );

		return true;

	}

	static const char* type_name ( const uint8_t* text, size_t start, size_t end ) {

		uint8_t c = text [ start ];

		if ( c == '{' )
			return "object";

		if ( c == '[' )
			return "array";

		if ( c == '"' )
			return "text";

		if ( c == 't' )
			return "true";

		if ( c == 'f' )
			return "false";

		if ( c == 'n' )
			return "null";

		for ( size_t at = start; at < end; at++ )
			if ( text [ at ] == '.' || text [ at ] == 'e' || text [ at ] == 'E' )
				return "real";

		return "integer";

	}

	bool append_json ( run_context& ctx, base::text& out, const storage::value& item ) {

		switch ( item.kind ) {

			case storage::VALUE_NULL:
				out.add ( "null" );

				return true;

			case storage::VALUE_INTEGER:
				out.signed_number ( item.integer );

				return true;

			case storage::VALUE_REAL:
				if ( item.real != item.real )
					out.add ( "null" );
				else if ( item.real > 1.7976931348623157e308 )
					out.add ( "9e999" );
				else if ( item.real < -1.7976931348623157e308 )
					out.add ( "-9e999" );
				else
					out.real ( item.real );

				return true;

			case storage::VALUE_DECIMAL:
				storage::write_decimal ( item, out );

				return true;

			case storage::VALUE_TEXT:
				if ( item.subtype == JSON_SUBTYPE ) {

					out.add ( reinterpret_cast < const char* > ( item.bytes ), item.size );

					return true;

				}

				out.add ( '"' );

				for ( size_t i = 0; i < item.size; i++ ) {

					uint8_t c = item.bytes [ i ];

					switch ( c ) {

						case '"': out.add ( "\\\"" ); break;
						case '\\': out.add ( "\\\\" ); break;
						case '\n': out.add ( "\\n" ); break;
						case '\r': out.add ( "\\r" ); break;
						case '\t': out.add ( "\\t" ); break;
						case '\b': out.add ( "\\b" ); break;
						case '\f': out.add ( "\\f" ); break;
						default:
							if ( c < 0x20 )
								out.add ( "\\u00" ).hex ( c, 2 );
							else
								out.add ( static_cast < char > ( c ) );

							break;

					}

				}

				out.add ( '"' );

				return true;

			default:
				return fail_run ( ctx, "JSON cannot hold a BLOB value" );

		}

	}

	static bool document_of ( run_context& ctx, const storage::value& item, storage::value& text ) {

		text = text_of ( *ctx.scratch, item );

		if ( !valid_json ( text.bytes, text.size ) )
			return fail_run ( ctx, "malformed JSON" );

		return true;

	}

	bool json_arrow ( run_context& ctx, const storage::value& document, const storage::value& path, bool value, storage::value& out ) {

		out = storage::null_value ( );

		if ( document.kind == storage::VALUE_NULL || path.kind == storage::VALUE_NULL )
			return true;

		storage::value text;

		if ( !document_of ( ctx, document, text ) )
			return false;

		base::text where;

		if ( path.kind == storage::VALUE_INTEGER )
			where.add ( "$[" ).signed_number ( path.integer ).add ( ']' );
		else {

			storage::value written = text_of ( *ctx.scratch, path );

			if ( written.size && written.bytes [ 0 ] == '$' )
				where.add ( reinterpret_cast < const char* > ( written.bytes ), written.size );
			else
				where.add ( "$.\"" ).add ( reinterpret_cast < const char* > ( written.bytes ), written.size ).add ( '"' );

		}

		size_t start = 0;
		size_t end = 0;
		bool found = false;

		if ( !walk ( ctx, text.bytes, text.size, reinterpret_cast < const uint8_t* > ( where.value ( ) ), where.size, start, end, found ) )
			return false;

		if ( !found )
			return true;

		if ( value )
			return to_value ( ctx, text.bytes, start, end, out );

		base::text made;

		minify ( text.bytes, start, end, made );

		return keep_text ( ctx, made, true, out );

	}

	bool call_json_function ( run_context& ctx, function_kind kind, const storage::value* args, uint32_t count, storage::value& out ) {

		out = storage::null_value ( );

		switch ( kind ) {

			case FUNCTION_JSON_ARRAY: {

				base::text made;

				made.add ( '[' );

				for ( uint32_t i = 0; i < count; i++ ) {

					if ( i )
						made.add ( ',' );

					if ( !append_json ( ctx, made, args [ i ] ) )
						return false;

				}

				made.add ( ']' );

				return keep_text ( ctx, made, true, out );

			}

			case FUNCTION_JSON_OBJECT: {

				if ( count % 2 )
					return fail_run ( ctx, "json_object() needs pairs of keys and values" );

				base::text made;

				made.add ( '{' );

				for ( uint32_t i = 0; i < count; i += 2 ) {

					if ( args [ i ].kind != storage::VALUE_TEXT )
						return fail_run ( ctx, "json_object() keys must be text" );

					storage::value key = args [ i ];

					key.subtype = 0;

					if ( i )
						made.add ( ',' );

					if ( !append_json ( ctx, made, key ) )
						return false;

					made.add ( ':' );

					if ( !append_json ( ctx, made, args [ i + 1 ] ) )
						return false;

				}

				made.add ( '}' );

				return keep_text ( ctx, made, true, out );

			}

			case FUNCTION_JSON_QUOTE: {

				base::text made;

				return append_json ( ctx, made, args [ 0 ] ) && keep_text ( ctx, made, true, out );

			}

			default:
				break;

		}

		if ( args [ 0 ].kind == storage::VALUE_NULL )
			return true;

		if ( kind == FUNCTION_JSON_VALID ) {

			storage::value text = text_of ( *ctx.scratch, args [ 0 ] );

			out = storage::integer_value ( valid_json ( text.bytes, text.size ) ? 1 : 0 );

			return true;

		}

		storage::value text;

		if ( !document_of ( ctx, args [ 0 ], text ) )
			return false;

		if ( kind == FUNCTION_JSON ) {

			base::text made;

			minify ( text.bytes, 0, text.size, made );

			return keep_text ( ctx, made, true, out );

		}

		auto locate = [ & ] ( const storage::value& path, size_t& start, size_t& end, bool& found ) {

			if ( path.kind == storage::VALUE_NULL ) {

				found = false;

				return true;

			}

			storage::value written = text_of ( *ctx.scratch, path );

			return walk ( ctx, text.bytes, text.size, written.bytes, written.size, start, end, found );

		};

		size_t start = 0;
		size_t end = 0;
		bool found = false;

		switch ( kind ) {

			case FUNCTION_JSON_EXTRACT: {

				if ( count == 2 ) {

					if ( !locate ( args [ 1 ], start, end, found ) )
						return false;

					return !found || to_value ( ctx, text.bytes, start, end, out );

				}

				base::text made;

				made.add ( '[' );

				for ( uint32_t i = 1; i < count; i++ ) {

					if ( !locate ( args [ i ], start, end, found ) )
						return false;

					if ( i > 1 )
						made.add ( ',' );

					if ( found )
						minify ( text.bytes, start, end, made );
					else
						made.add ( "null" );

				}

				made.add ( ']' );

				return keep_text ( ctx, made, true, out );

			}

			case FUNCTION_JSON_TYPE:
			case FUNCTION_JSON_ARRAY_LENGTH: {

				storage::value root = storage::text_value ( "$", 1 );

				if ( !locate ( count > 1 ? args [ 1 ] : root, start, end, found ) )
					return false;

				if ( !found )
					return true;

				if ( kind == FUNCTION_JSON_TYPE ) {

					const char* name = type_name ( text.bytes, start, end );

					out = storage::text_value ( name, base::length ( name ) );

					return true;

				}

				if ( text.bytes [ start ] != '[' ) {

					out = storage::integer_value ( 0 );

					return true;

				}

				int64_t length = 0;
				size_t cursor = start + 1;

				skip_space ( text.bytes, end, cursor );

				while ( cursor < end && text.bytes [ cursor ] != ']' ) {

					skip_value ( text.bytes, end, cursor, 0 );
					length++;
					skip_space ( text.bytes, end, cursor );

					if ( cursor < end && text.bytes [ cursor ] == ',' )
						cursor++;

					skip_space ( text.bytes, end, cursor );

				}

				out = storage::integer_value ( length );

				return true;

			}

			default:
				return fail_run ( ctx, "this JSON function cannot run here" );

		}

	}

}
