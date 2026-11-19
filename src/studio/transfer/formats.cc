#include "studio/transfer/transfer.hh"

namespace studio {

	static void reset_table ( import_table& out ) {

		out.memory.reset ( );
		out.names = nullptr;
		out.types = nullptr;
		out.column_count = 0;
		out.cells = nullptr;
		out.cell_count = 0;
		out.cell_room = 0;
		out.rows = 0;

	}

	static bool push_cell ( import_table& out, const char* value, size_t size, bool null ) {

		const char* kept = null ? nullptr : out.memory.keep ( value, size );

		if ( !null && !kept )
			return false;

		return base::push ( out.memory, out.cells, out.cell_count, out.cell_room, kept );

	}

	static const char* unique_name ( import_table& out, const char* const* names, uint32_t count, const char* wanted, size_t size ) {

		base::text name;

		if ( !size )
			name.add ( "column" ).number ( count + 1 );
		else
			name.add ( wanted, size );

		for ( uint32_t suffix = 2; ; suffix++ ) {

			bool taken = false;

			for ( uint32_t i = 0; i < count && !taken; i++ )
				taken = base::same_folded ( names [ i ], name.value ( ) );

			if ( !taken )
				break;

			name.clear ( );
			name.add ( wanted, size ).add ( '_' ).number ( suffix );

		}

		return out.memory.keep ( name.value ( ), name.size );

	}

	static bool numeric_text ( const char* text, bool& whole ) {

		size_t at = 0;
		bool digits = false;

		whole = true;

		if ( text [ at ] == '-' || text [ at ] == '+' )
			at++;

		while ( text [ at ] >= '0' && text [ at ] <= '9' ) {

			at++;
			digits = true;

		}

		if ( text [ at ] == '.' ) {

			whole = false;
			at++;

			while ( text [ at ] >= '0' && text [ at ] <= '9' ) {

				at++;
				digits = true;

			}

		}

		if ( digits && ( text [ at ] == 'e' || text [ at ] == 'E' ) ) {

			whole = false;
			at++;

			if ( text [ at ] == '-' || text [ at ] == '+' )
				at++;

			if ( text [ at ] < '0' || text [ at ] > '9' )
				return false;

			while ( text [ at ] >= '0' && text [ at ] <= '9' )
				at++;

		}

		return digits && !text [ at ] && at < 19;

	}

	void infer_types ( import_table& table ) {

		table.types = table.memory.make < sql::affinity_kind > ( table.column_count + 1 );

		if ( !table.types )
			return;

		for ( uint32_t c = 0; c < table.column_count; c++ ) {

			bool any = false;
			bool integers = true;
			bool numbers = true;

			for ( uint32_t r = 0; r < table.rows; r++ ) {

				const char* cell = table.cells [ static_cast < size_t > ( r ) * table.column_count + c ];
				bool whole = false;

				if ( !cell )
					continue;

				any = true;

				if ( !numeric_text ( cell, whole ) ) {

					integers = false;
					numbers = false;

					break;

				}

				integers = integers && whole;

			}

			table.types [ c ] = !any ? sql::AFFINITY_TEXT : integers ? sql::AFFINITY_INTEGER : numbers ? sql::AFFINITY_REAL : sql::AFFINITY_TEXT;

		}

	}

	static char pick_separator ( const char* text, size_t size ) {

		uint32_t counts [ 3 ] = { };
		static const char separators [ 3 ] = { ',', ';', '\t' };
		bool quoted = false;

		for ( size_t i = 0; i < size && text [ i ] != '\n'; i++ ) {

			if ( text [ i ] == '"' )
				quoted = !quoted;

			for ( uint32_t s = 0; s < 3 && !quoted; s++ )
				counts [ s ] += text [ i ] == separators [ s ];

		}

		uint32_t best = 0;

		for ( uint32_t s = 1; s < 3; s++ )
			if ( counts [ s ] > counts [ best ] )
				best = s;

		return separators [ best ];

	}

	bool parse_csv ( const char* text, size_t size, import_table& out, base::text& problem ) {

		reset_table ( out );

		if ( size >= 3 && static_cast < uint8_t > ( text [ 0 ] ) == 0xEF && static_cast < uint8_t > ( text [ 1 ] ) == 0xBB && static_cast < uint8_t > ( text [ 2 ] ) == 0xBF ) {

			text += 3;
			size -= 3;

		}

		char separator = pick_separator ( text, size );
		const char** names = nullptr;
		uint32_t name_count = 0;
		uint32_t name_room = 0;
		uint32_t field = 0;
		bool header = true;
		size_t at = 0;
		base::text cell;

		while ( at < size ) {

			bool quoted = false;
			bool row_end = false;

			cell.clear ( );

			if ( text [ at ] == '"' ) {

				quoted = true;
				at++;

				while ( at < size ) {

					if ( text [ at ] == '"' ) {

						if ( at + 1 < size && text [ at + 1 ] == '"' ) {

							cell.add ( '"' );
							at += 2;

							continue;

						}

						at++;

						break;

					}

					cell.add ( text [ at++ ] );

				}

			}

			while ( at < size && text [ at ] != separator && text [ at ] != '\n' && text [ at ] != '\r' )
				cell.add ( text [ at++ ] );

			if ( at < size && text [ at ] == separator )
				at++;
			else {

				row_end = true;

				if ( at < size && text [ at ] == '\r' )
					at++;

				if ( at < size && text [ at ] == '\n' )
					at++;

			}

			if ( header ) {

				size_t start = 0;
				size_t end = cell.size;

				while ( start < end && cell.data [ start ] == ' ' )
					start++;

				while ( end > start && cell.data [ end - 1 ] == ' ' )
					end--;

				const char* name = unique_name ( out, names, name_count, cell.value ( ) + start, end - start );

				if ( !name || name_count == IMPORT_COLUMNS || !base::push ( out.memory, names, name_count, name_room, name ) ) {

					problem.add ( "The first row has too many columns" );

					return false;

				}

			} else if ( field < name_count ) {

				if ( !push_cell ( out, cell.value ( ), cell.size, !quoted && !cell.size ) ) {

					problem.add ( "Out of memory while reading the rows" );

					return false;

				}

			}

			field++;

			if ( !row_end )
				continue;

			if ( header ) {

				header = false;
				out.names = names;
				out.column_count = name_count;

			} else if ( field == 1 && !quoted && !cell.size ) {

				out.cell_count -= name_count ? 1 : 0;

			} else {

				for ( ; field < name_count; field++ )
					push_cell ( out, "", 0, true );

				out.rows++;

			}

			field = 0;

		}

		if ( header && name_count ) {

			out.names = names;
			out.column_count = name_count;

		}

		if ( !out.column_count ) {

			problem.add ( "There is nothing to read" );

			return false;

		}

		if ( out.cell_count != out.rows * out.column_count )
			out.cell_count = out.rows * out.column_count;

		infer_types ( out );

		return true;

	}

	static void skip_space ( const char* text, size_t size, size_t& at ) {

		while ( at < size && ( text [ at ] == ' ' || text [ at ] == '\n' || text [ at ] == '\r' || text [ at ] == '\t' ) )
			at++;

	}

	static bool read_string ( const char* text, size_t size, size_t& at, base::text& out ) {

		out.clear ( );

		if ( at >= size || text [ at ] != '"' )
			return false;

		at++;

		while ( at < size && text [ at ] != '"' ) {

			if ( text [ at ] != '\\' ) {

				out.add ( text [ at++ ] );

				continue;

			}

			if ( ++at >= size )
				return false;

			char escape = text [ at++ ];

			switch ( escape ) {

				case 'n': out.add ( '\n' ); break;
				case 't': out.add ( '\t' ); break;
				case 'r': out.add ( '\r' ); break;
				case 'b': out.add ( '\b' ); break;
				case 'f': out.add ( '\f' ); break;

				case 'u': {

					uint32_t code = 0;

					for ( uint32_t k = 0; k < 4; k++, at++ ) {

						if ( at >= size )
							return false;

						char digit = base::lower ( text [ at ] );

						code = code * 16 + static_cast < uint32_t > ( digit >= 'a' ? digit - 'a' + 10 : digit - '0' );

					}

					if ( code >= 0xD800 && code < 0xDC00 && at + 6 <= size && text [ at ] == '\\' && text [ at + 1 ] == 'u' ) {

						uint32_t low = 0;

						for ( uint32_t k = 2; k < 6; k++ ) {

							char digit = base::lower ( text [ at + k ] );

							low = low * 16 + static_cast < uint32_t > ( digit >= 'a' ? digit - 'a' + 10 : digit - '0' );

						}

						code = 0x10000 + ( ( code - 0xD800 ) << 10 ) + ( low - 0xDC00 );
						at += 6;

					}

					out.code ( code );

					break;

				}

				default:
					out.add ( escape );
					break;

			}

		}

		if ( at >= size )
			return false;

		at++;

		return true;

	}

	static bool skip_value ( const char* text, size_t size, size_t& at, uint32_t depth ) {

		skip_space ( text, size, at );

		if ( at >= size || depth > 64 )
			return false;

		if ( text [ at ] == '"' ) {

			base::text ignored;

			return read_string ( text, size, at, ignored );

		}

		if ( text [ at ] == '{' || text [ at ] == '[' ) {

			char close = text [ at ] == '{' ? '}' : ']';

			at++;
			skip_space ( text, size, at );

			if ( at < size && text [ at ] == close ) {

				at++;

				return true;

			}

			for ( ;; ) {

				if ( close == '}' ) {

					base::text key;

					skip_space ( text, size, at );

					if ( !read_string ( text, size, at, key ) )
						return false;

					skip_space ( text, size, at );

					if ( at >= size || text [ at++ ] != ':' )
						return false;

				}

				if ( !skip_value ( text, size, at, depth + 1 ) )
					return false;

				skip_space ( text, size, at );

				if ( at < size && text [ at ] == ',' ) {

					at++;

					continue;

				}

				if ( at < size && text [ at ] == close ) {

					at++;

					return true;

				}

				return false;

			}

		}

		while ( at < size && text [ at ] != ',' && text [ at ] != '}' && text [ at ] != ']' && text [ at ] != ' ' && text [ at ] != '\n' && text [ at ] != '\r' && text [ at ] != '\t' )
			at++;

		return true;

	}

	static int32_t column_named ( import_table& out, const char**& names, uint32_t& room, const char* name, size_t size ) {

		for ( uint32_t i = 0; i < out.column_count; i++ )
			if ( base::length ( names [ i ] ) == size && base::same_bytes ( names [ i ], name, size ) )
				return static_cast < int32_t > ( i );

		if ( out.column_count == IMPORT_COLUMNS )
			return -1;

		const char* kept = unique_name ( out, names, out.column_count, name, size );

		if ( !kept || !base::push ( out.memory, names, out.column_count, room, kept ) )
			return -1;

		return static_cast < int32_t > ( out.column_count - 1 );

	}

	bool parse_json_rows ( const char* text, size_t size, import_table& out, base::text& problem ) {

		reset_table ( out );

		size_t at = 0;
		const char** names = nullptr;
		uint32_t room = 0;

		skip_space ( text, size, at );

		bool single = at < size && text [ at ] == '{';

		if ( at >= size || ( text [ at ] != '[' && !single ) ) {

			problem.add ( "JSON imports need an array of objects" );

			return false;

		}

		if ( !single )
			at++;

		for ( uint32_t pass = 0; pass < 2; pass++ ) {

			size_t walk = at;

			if ( pass == 1 ) {

				out.names = names;
				out.cells = out.memory.make < const char* > ( static_cast < size_t > ( out.rows ) * out.column_count + 1 );
				out.cell_count = out.rows * out.column_count;
				out.rows = 0;

				if ( !out.cells ) {

					problem.add ( "Out of memory while reading the rows" );

					return false;

				}

			}

			for ( ;; ) {

				skip_space ( text, size, walk );

				if ( walk < size && text [ walk ] == ']' )
					break;

				if ( walk >= size || text [ walk ] != '{' ) {

					problem.add ( "Each row in the JSON array has to be an object" );

					return false;

				}

				walk++;

				for ( ;; ) {

					base::text key;

					skip_space ( text, size, walk );

					if ( walk < size && text [ walk ] == '}' ) {

						walk++;

						break;

					}

					if ( !read_string ( text, size, walk, key ) ) {

						problem.add ( "The JSON has a damaged key" );

						return false;

					}

					skip_space ( text, size, walk );

					if ( walk >= size || text [ walk++ ] != ':' ) {

						problem.add ( "The JSON is missing a colon" );

						return false;

					}

					skip_space ( text, size, walk );

					size_t start = walk;
					int32_t column = pass == 0 ? column_named ( out, names, room, key.value ( ), key.size ) : -1;

					if ( pass == 1 )
						for ( uint32_t i = 0; i < out.column_count && column < 0; i++ )
							if ( base::length ( names [ i ] ) == key.size && base::same_bytes ( names [ i ], key.value ( ), key.size ) )
								column = static_cast < int32_t > ( i );

					if ( walk < size && text [ walk ] == '"' ) {

						base::text value;

						if ( !read_string ( text, size, walk, value ) ) {

							problem.add ( "The JSON has a damaged string" );

							return false;

						}

						if ( pass == 1 && column >= 0 )
							out.cells [ static_cast < size_t > ( out.rows ) * out.column_count + column ] = out.memory.keep ( value.value ( ), value.size );

					} else {

						if ( !skip_value ( text, size, walk, 0 ) ) {

							problem.add ( "The JSON has a damaged value" );

							return false;

						}

						if ( pass == 1 && column >= 0 ) {

							const char* value = text + start;
							size_t length = walk - start;
							const char** slot = &out.cells [ static_cast < size_t > ( out.rows ) * out.column_count + column ];

							if ( length == 4 && base::same_bytes ( value, "null", 4 ) )
								*slot = nullptr;
							else if ( length == 4 && base::same_bytes ( value, "true", 4 ) )
								*slot = "1";
							else if ( length == 5 && base::same_bytes ( value, "false", 5 ) )
								*slot = "0";
							else
								*slot = out.memory.keep ( value, length );

						}

					}

					skip_space ( text, size, walk );

					if ( walk < size && text [ walk ] == ',' )
						walk++;

				}

				out.rows++;
				skip_space ( text, size, walk );

				if ( walk < size && text [ walk ] == ',' )
					walk++;

				if ( single )
					break;

			}

		}

		if ( !out.column_count ) {

			problem.add ( "The JSON objects have no keys to make columns from" );

			return false;

		}

		infer_types ( out );

		return true;

	}

	static void csv_value ( base::text& out, const storage::value& value ) {

		if ( value.kind == storage::VALUE_NULL )
			return;

		base::text plain;

		engine::describe_value ( plain, value );

		bool quote = false;

		for ( size_t i = 0; i < plain.size && !quote; i++ )
			quote = plain.data [ i ] == ',' || plain.data [ i ] == '"' || plain.data [ i ] == '\n' || plain.data [ i ] == '\r';

		quote = quote || ( plain.size && ( plain.data [ 0 ] == ' ' || plain.data [ plain.size - 1 ] == ' ' ) );

		if ( !quote ) {

			out.add ( plain.value ( ), plain.size );

			return;

		}

		out.add ( '"' );

		for ( size_t i = 0; i < plain.size; i++ ) {

			if ( plain.data [ i ] == '"' )
				out.add ( '"' );

			out.add ( plain.data [ i ] );

		}

		out.add ( '"' );

	}

	static void json_text ( base::text& out, const char* text, size_t size ) {

		out.add ( '"' );

		for ( size_t i = 0; i < size; i++ ) {

			uint8_t byte = static_cast < uint8_t > ( text [ i ] );

			if ( byte == '"' || byte == '\\' )
				out.add ( '\\' ).add ( static_cast < char > ( byte ) );
			else if ( byte == '\n' )
				out.add ( "\\n" );
			else if ( byte == '\r' )
				out.add ( "\\r" );
			else if ( byte == '\t' )
				out.add ( "\\t" );
			else if ( byte < 0x20 )
				out.add ( "\\u00" ).hex ( byte, 2 );
			else
				out.add ( static_cast < char > ( byte ) );

		}

		out.add ( '"' );

	}

	void write_csv_row ( base::text& out, const storage::value* values, uint32_t count ) {

		for ( uint32_t c = 0; c < count; c++ ) {

			if ( c )
				out.add ( ',' );

			csv_value ( out, values [ c ] );

		}

		out.add ( "\r\n" );

	}

	void write_json_row ( base::text& out, const char* const* names, const storage::value* values, uint32_t count, bool first ) {

		out.add ( first ? "\n  { " : ",\n  { " );

		for ( uint32_t c = 0; c < count; c++ ) {

			if ( c )
				out.add ( ", " );

			json_text ( out, names [ c ], base::length ( names [ c ] ) );
			out.add ( ": " );

			const storage::value& value = values [ c ];

			if ( value.kind == storage::VALUE_NULL )
				out.add ( "null" );
			else if ( value.kind == storage::VALUE_INTEGER || value.kind == storage::VALUE_REAL )
				engine::describe_value ( out, value );
			else if ( value.kind == storage::VALUE_TEXT )
				json_text ( out, reinterpret_cast < const char* > ( value.bytes ), value.size );
			else {

				base::text hex;

				engine::describe_value ( hex, value );
				json_text ( out, hex.value ( ), hex.size );

			}

		}

		out.add ( " }" );

	}

	void write_csv_header ( base::text& out, const char* const* names, uint32_t count ) {

		for ( uint32_t c = 0; c < count; c++ ) {

			storage::value name = storage::text_value ( names [ c ], base::length ( names [ c ] ) );

			if ( c )
				out.add ( ',' );

			csv_value ( out, name );

		}

		out.add ( "\r\n" );

	}

}