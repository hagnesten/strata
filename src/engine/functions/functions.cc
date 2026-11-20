#include "engine/run/run.hh"

namespace engine {

	static const function_entry functions [ ] = {

		{ "abs", 1, 1, false, FUNCTION_ABS },
		{ "changes", 0, 0, false, FUNCTION_CHANGES },
		{ "char", 0, 255, false, FUNCTION_CHAR },
		{ "coalesce", 1, 255, false, FUNCTION_COALESCE },
		{ "concat", 1, 255, false, FUNCTION_CONCAT },
		{ "concat_ws", 2, 255, false, FUNCTION_CONCAT_WS },
		{ "format", 1, 255, false, FUNCTION_PRINTF },
		{ "glob", 2, 2, false, FUNCTION_GLOB },
		{ "hex", 1, 1, false, FUNCTION_HEX },
		{ "ifnull", 2, 2, false, FUNCTION_IFNULL },
		{ "iif", 3, 3, false, FUNCTION_IIF },
		{ "instr", 2, 2, false, FUNCTION_INSTR },
		{ "last_insert_rowid", 0, 0, false, FUNCTION_LAST_ROWID },
		{ "length", 1, 1, false, FUNCTION_LENGTH },
		{ "like", 2, 3, false, FUNCTION_LIKE },
		{ "lower", 1, 1, false, FUNCTION_LOWER },
		{ "ltrim", 1, 2, false, FUNCTION_LTRIM },
		{ "max", 2, 255, false, FUNCTION_MAX },
		{ "min", 2, 255, false, FUNCTION_MIN },
		{ "nullif", 2, 2, false, FUNCTION_NULLIF },
		{ "octet_length", 1, 1, false, FUNCTION_OCTET_LENGTH },
		{ "printf", 1, 255, false, FUNCTION_PRINTF },
		{ "quote", 1, 1, false, FUNCTION_QUOTE },
		{ "random", 0, 0, false, FUNCTION_RANDOM },
		{ "randomblob", 1, 1, false, FUNCTION_RANDOMBLOB },
		{ "replace", 3, 3, false, FUNCTION_REPLACE },
		{ "round", 1, 2, false, FUNCTION_ROUND },
		{ "rtrim", 1, 2, false, FUNCTION_RTRIM },
		{ "sign", 1, 1, false, FUNCTION_SIGN },
		{ "strata_version", 0, 0, false, FUNCTION_VERSION },
		{ "substr", 2, 3, false, FUNCTION_SUBSTR },
		{ "substring", 2, 3, false, FUNCTION_SUBSTR },
		{ "total_changes", 0, 0, false, FUNCTION_TOTAL_CHANGES },
		{ "trim", 1, 2, false, FUNCTION_TRIM },
		{ "typeof", 1, 1, false, FUNCTION_TYPEOF },
		{ "unicode", 1, 1, false, FUNCTION_UNICODE },
		{ "upper", 1, 1, false, FUNCTION_UPPER },
		{ "zeroblob", 1, 1, false, FUNCTION_ZEROBLOB },
		{ "version", 0, 0, false, FUNCTION_SERVER_VERSION },
		{ "current_database", 0, 0, false, FUNCTION_CURRENT_DATABASE },
		{ "current_catalog", 0, 0, false, FUNCTION_CURRENT_DATABASE },
		{ "current_schema", 0, 0, false, FUNCTION_CURRENT_SCHEMA },
		{ "current_user", 0, 0, false, FUNCTION_CURRENT_USER },
		{ "session_user", 0, 0, false, FUNCTION_CURRENT_USER },
		{ "pg_backend_pid", 0, 0, false, FUNCTION_BACKEND_PID },
		{ "current_setting", 1, 2, false, FUNCTION_CURRENT_SETTING },
		{ "gen_random_uuid", 0, 0, false, FUNCTION_RANDOM_UUID },
		{ "uuid_generate_v4", 0, 0, false, FUNCTION_RANDOM_UUID },
		{ "pg_advisory_unlock_all", 0, 0, false, FUNCTION_NOTHING },
		{ "pg_advisory_lock", 1, 2, false, FUNCTION_NOTHING },
		{ "pg_advisory_unlock", 1, 2, false, FUNCTION_NOTHING },
		{ "pg_advisory_xact_lock", 1, 2, false, FUNCTION_NOTHING },
		{ "set_config", 3, 3, false, FUNCTION_NOTHING },
		{ "now", 0, 0, false, FUNCTION_NOW },
		{ "strata_any", 2, 2, false, FUNCTION_ARRAY_ANY },
		{ "date_part", 2, 2, false, FUNCTION_DATE_PART },
		{ "date_trunc", 2, 3, false, FUNCTION_DATE_TRUNC },
		{ "statement_timestamp", 0, 0, false, FUNCTION_NOW },
		{ "transaction_timestamp", 0, 0, false, FUNCTION_NOW },
		{ "clock_timestamp", 0, 0, false, FUNCTION_NOW },
		{ "date", 0, 255, false, FUNCTION_DATE },
		{ "time", 0, 255, false, FUNCTION_TIME },
		{ "datetime", 0, 255, false, FUNCTION_DATETIME },
		{ "julianday", 0, 255, false, FUNCTION_JULIANDAY },
		{ "unixepoch", 0, 255, false, FUNCTION_UNIXEPOCH },
		{ "strftime", 1, 255, false, FUNCTION_STRFTIME },
		{ "json", 1, 1, false, FUNCTION_JSON },
		{ "json_valid", 1, 1, false, FUNCTION_JSON_VALID },
		{ "json_extract", 2, 255, false, FUNCTION_JSON_EXTRACT },
		{ "json_type", 1, 2, false, FUNCTION_JSON_TYPE },
		{ "json_array", 0, 255, false, FUNCTION_JSON_ARRAY },
		{ "json_object", 0, 255, false, FUNCTION_JSON_OBJECT },
		{ "json_array_length", 1, 2, false, FUNCTION_JSON_ARRAY_LENGTH },
		{ "json_quote", 1, 1, false, FUNCTION_JSON_QUOTE },
		{ "bm25", 1, 255, false, FUNCTION_BM25 },
		{ "highlight", 4, 4, false, FUNCTION_HIGHLIGHT },
		{ "snippet", 1, 6, false, FUNCTION_SNIPPET },
		{ "json_group_array", 1, 1, true, FUNCTION_JSON_GROUP_ARRAY },
		{ "json_group_object", 2, 2, true, FUNCTION_JSON_GROUP_OBJECT },
		{ "count", 0, 1, true, FUNCTION_COUNT },
		{ "sum", 1, 1, true, FUNCTION_SUM },
		{ "total", 1, 1, true, FUNCTION_TOTAL },
		{ "avg", 1, 1, true, FUNCTION_AVG },
		{ "min", 1, 1, true, FUNCTION_MIN_AGGREGATE },
		{ "max", 1, 1, true, FUNCTION_MAX_AGGREGATE },
		{ "group_concat", 1, 2, true, FUNCTION_GROUP_CONCAT },
		{ "string_agg", 2, 2, true, FUNCTION_GROUP_CONCAT },
		{ "row_number", 0, 0, false, FUNCTION_ROW_NUMBER },
		{ "rank", 0, 0, false, FUNCTION_RANK },
		{ "dense_rank", 0, 0, false, FUNCTION_DENSE_RANK },
		{ "percent_rank", 0, 0, false, FUNCTION_PERCENT_RANK },
		{ "cume_dist", 0, 0, false, FUNCTION_CUME_DIST },
		{ "ntile", 1, 1, false, FUNCTION_NTILE },
		{ "lag", 1, 3, false, FUNCTION_LAG },
		{ "lead", 1, 3, false, FUNCTION_LEAD },
		{ "first_value", 1, 1, false, FUNCTION_FIRST_VALUE },
		{ "last_value", 1, 1, false, FUNCTION_LAST_VALUE },
		{ "nth_value", 2, 2, false, FUNCTION_NTH_VALUE },

	};

	int32_t function_id ( const char* name, uint32_t args, bool star, bool& aggregate ) {

		aggregate = false;

		if ( star ) {

			if ( !base::same_folded ( name, "count" ) )
				return -1;

			aggregate = true;

			return FUNCTION_COUNT;

		}

		for ( const function_entry& entry : functions ) {

			if ( !base::same_folded ( entry.name, name ) || args < entry.low || args > entry.high )
				continue;

			if ( entry.kind == FUNCTION_COUNT && args != 1 )
				continue;

			aggregate = entry.aggregate;

			return entry.kind;

		}

		return -1;

	}

	bool window_function ( int32_t id ) {

		return id >= FUNCTION_ROW_NUMBER;

	}

	bool function_named ( const char* name ) {

		for ( const function_entry& entry : functions )
			if ( base::same_folded ( entry.name, name ) )
				return true;

		return false;

	}

	static bool make_text ( run_context& ctx, const base::text& made, storage::value& out ) {

		char* kept = ctx.scratch->keep ( made.value ( ), made.size );

		if ( !kept )
			return fail_run ( ctx, "out of memory" );

		out = storage::text_value ( kept, made.size );

		return true;

	}

	static uint64_t next_random ( run_context& ctx ) {

		uint64_t state = *ctx.random;

		state ^= state << 13;
		state ^= state >> 7;
		state ^= state << 17;
		*ctx.random = state;

		return state;

	}

	static bool trim_text ( run_context& ctx, const storage::value* args, uint32_t count, bool left, bool right, storage::value& out ) {

		if ( args [ 0 ].kind == storage::VALUE_NULL || ( count > 1 && args [ 1 ].kind == storage::VALUE_NULL ) ) {

			out = storage::null_value ( );

			return true;

		}

		storage::value text = text_of ( *ctx.scratch, args [ 0 ] );
		storage::value set = count > 1 ? text_of ( *ctx.scratch, args [ 1 ] ) : storage::text_value ( " ", 1 );
		size_t start = 0;
		size_t end = text.size;

		auto in_set = [ & ] ( uint32_t code ) {

			size_t at = 0;

			while ( at < set.size )
				if ( base::next_code ( set.bytes, set.size, at ) == code )
					return true;

			return false;

		};

		while ( left && start < end ) {

			size_t at = start;
			uint32_t code = base::next_code ( text.bytes, end, at );

			if ( !in_set ( code ) )
				break;

			start = at;

		}

		while ( right && end > start ) {

			size_t back = end - 1;

			while ( back > start && ( text.bytes [ back ] & 0xC0 ) == 0x80 )
				back--;

			size_t at = back;
			uint32_t code = base::next_code ( text.bytes, end, at );

			if ( !in_set ( code ) )
				break;

			end = back;

		}

		out = storage::text_value ( reinterpret_cast < const char* > ( text.bytes + start ), end - start );

		return true;

	}

	static bool substring ( run_context& ctx, const storage::value* args, uint32_t count, storage::value& out ) {

		if ( args [ 0 ].kind == storage::VALUE_NULL || args [ 1 ].kind == storage::VALUE_NULL || ( count > 2 && args [ 2 ].kind == storage::VALUE_NULL ) ) {

			out = storage::null_value ( );

			return true;

		}

		bool blob = args [ 0 ].kind == storage::VALUE_BLOB;
		storage::value text = blob ? args [ 0 ] : text_of ( *ctx.scratch, args [ 0 ] );
		int64_t length = blob ? static_cast < int64_t > ( text.size ) : static_cast < int64_t > ( base::count_codes ( text.bytes, text.size ) );
		int64_t first = 0;
		int64_t wanted = 0x3FFFFFFFFFFFFFFFll;
		bool negative = false;

		integer_of ( args [ 1 ], first );

		if ( count > 2 ) {

			integer_of ( args [ 2 ], wanted );

			if ( wanted < 0 ) {

				negative = true;
				wanted = wanted == static_cast < int64_t > ( 0x8000000000000000ull ) ? 0x3FFFFFFFFFFFFFFFll : -wanted;

			}

		}

		if ( first < 0 ) {

			first += length;

			if ( first < 0 ) {

				wanted += first;

				if ( wanted < 0 )
					wanted = 0;

				first = 0;

			}

		} else if ( first > 0 )
			first--;
		else if ( wanted > 0 )
			wanted--;

		if ( negative ) {

			first -= wanted;

			if ( first < 0 ) {

				wanted += first;
				first = 0;

			}

		}

		if ( first > length )
			first = length;

		if ( wanted > length - first )
			wanted = length - first;

		if ( wanted < 0 )
			wanted = 0;

		if ( blob ) {

			out = storage::blob_value ( text.bytes + first, static_cast < size_t > ( wanted ) );

			return true;

		}

		size_t start = base::skip_codes ( text.bytes, text.size, static_cast < size_t > ( first ) );
		size_t size = base::skip_codes ( text.bytes + start, text.size - start, static_cast < size_t > ( wanted ) );

		out = storage::text_value ( reinterpret_cast < const char* > ( text.bytes + start ), size );

		return true;

	}

	static void add_quoted ( base::text& out, const storage::value& item ) {

		switch ( item.kind ) {

			case storage::VALUE_NULL:
				out.add ( "NULL" );
				break;

			case storage::VALUE_INTEGER:
				out.signed_number ( item.integer );
				break;

			case storage::VALUE_REAL:
				out.real ( item.real );
				break;

			case storage::VALUE_DECIMAL:
				storage::write_decimal ( item, out );
				break;

			case storage::VALUE_TEXT:
				out.add ( '\'' );

				for ( size_t i = 0; i < item.size; i++ ) {

					if ( item.bytes [ i ] == '\'' )
						out.add ( '\'' );

					out.add ( static_cast < char > ( item.bytes [ i ] ) );

				}

				out.add ( '\'' );
				break;

			case storage::VALUE_BLOB:
				out.add ( "X'" );

				for ( size_t i = 0; i < item.size; i++ )
					out.hex ( item.bytes [ i ], 2, true );

				out.add ( '\'' );
				break;

		}

	}

	static void add_fixed ( base::text& out, double value, uint32_t places ) {

		if ( value != value ) {

			out.add ( "NaN" );

			return;

		}

		if ( value < 0 ) {

			out.add ( '-' );
			value = -value;

		}

		places = base::min < uint32_t > ( places, 18 );

		uint64_t unit = 1;

		for ( uint32_t i = 0; i < places; i++ )
			unit *= 10;

		if ( value * static_cast < double > ( unit ) >= 1.8e19 ) {

			out.real ( value );

			return;

		}

		out.decimal ( static_cast < uint64_t > ( value * static_cast < double > ( unit ) + 0.5 ), unit, places );

	}

	static bool add_special ( base::text& out, double& value ) {

		if ( value != value ) {

			out.add ( "NaN" );

			return true;

		}

		if ( value < 0 ) {

			out.add ( '-' );
			value = -value;

		}

		if ( value > 1.7976931348623157e308 ) {

			out.add ( "Inf" );

			return true;

		}

		return false;

	}

	static void add_exponent ( base::text& out, int32_t exponent, bool upper ) {

		out.add ( upper ? 'E' : 'e' ).add ( exponent < 0 ? '-' : '+' );

		uint32_t magnitude = static_cast < uint32_t > ( exponent < 0 ? -exponent : exponent );

		if ( magnitude < 10 )
			out.add ( '0' );

		out.number ( magnitude );

	}

	static void add_scientific ( base::text& out, double value, uint32_t places, bool upper ) {

		if ( add_special ( out, value ) )
			return;

		char digits [ 18 ];
		int32_t exponent = 0;
		uint32_t count = base::min < uint32_t > ( places + 1, 17 );

		if ( value == 0 )
			base::fill ( digits, '0', sizeof ( digits ) );
		else
			base::significant_digits ( value, count, digits, exponent );

		out.add ( digits [ 0 ] );

		if ( places ) {

			out.add ( '.' ).add ( digits + 1, count - 1 );
			out.repeat ( '0', places + 1 - count );

		}

		add_exponent ( out, exponent, upper );

	}

	static void add_general ( base::text& out, double value, int32_t precision, bool upper, bool alternate ) {

		if ( add_special ( out, value ) )
			return;

		uint32_t significant = precision < 0 ? 6 : precision == 0 ? 1 : base::min < uint32_t > ( static_cast < uint32_t > ( precision ), 17 );
		char digits [ 18 ];
		int32_t exponent = 0;

		if ( value == 0 )
			base::fill ( digits, '0', sizeof ( digits ) );
		else
			base::significant_digits ( value, significant, digits, exponent );

		uint32_t count = significant;

		if ( !alternate )
			while ( count > 1 && digits [ count - 1 ] == '0' )
				count--;

		if ( exponent < -4 || exponent >= static_cast < int32_t > ( significant ) ) {

			out.add ( digits [ 0 ] );

			if ( count > 1 )
				out.add ( '.' ).add ( digits + 1, count - 1 );

			add_exponent ( out, exponent, upper );

			return;

		}

		if ( exponent < 0 ) {

			out.add ( "0." ).repeat ( '0', static_cast < size_t > ( -exponent - 1 ) ).add ( digits, count );

			return;

		}

		uint32_t whole = static_cast < uint32_t > ( exponent ) + 1;

		for ( uint32_t i = 0; i < whole; i++ )
			out.add ( i < count ? digits [ i ] : '0' );

		if ( count > whole )
			out.add ( '.' ).add ( digits + whole, count - whole );

	}

	static bool format_text ( run_context& ctx, const storage::value* args, uint32_t count, storage::value& out ) {

		if ( args [ 0 ].kind == storage::VALUE_NULL ) {

			out = storage::null_value ( );

			return true;

		}

		storage::value pattern = text_of ( *ctx.scratch, args [ 0 ] );
		base::text made;
		uint32_t next = 1;

		for ( size_t i = 0; i < pattern.size; i++ ) {

			char here = static_cast < char > ( pattern.bytes [ i ] );

			if ( here != '%' || i + 1 >= pattern.size ) {

				made.add ( here );

				continue;

			}

			i++;

			bool left = false;
			bool zero = false;
			bool plus = false;
			bool space = false;
			bool alternate = false;

			for ( ; i < pattern.size; i++ ) {

				char flag = static_cast < char > ( pattern.bytes [ i ] );

				if ( flag == '-' )
					left = true;
				else if ( flag == '0' )
					zero = true;
				else if ( flag == '+' )
					plus = true;
				else if ( flag == ' ' )
					space = true;
				else if ( flag == '#' )
					alternate = true;
				else if ( flag != ',' && flag != '!' )
					break;

			}

			uint32_t width = 0;
			int32_t precision = -1;

			if ( i < pattern.size && pattern.bytes [ i ] == '*' ) {

				int64_t taken = 0;

				if ( next < count )
					integer_of ( args [ next++ ], taken );

				if ( taken < 0 ) {

					left = true;
					taken = -taken;

				}

				width = static_cast < uint32_t > ( base::min < int64_t > ( taken, 10000 ) );
				i++;

			} else {

				while ( i < pattern.size && pattern.bytes [ i ] >= '0' && pattern.bytes [ i ] <= '9' )
					width = base::min < uint32_t > ( width * 10 + ( pattern.bytes [ i++ ] - '0' ), 10000 );

			}

			if ( i < pattern.size && pattern.bytes [ i ] == '.' ) {

				i++;
				precision = 0;

				if ( i < pattern.size && pattern.bytes [ i ] == '*' ) {

					int64_t taken = 0;

					if ( next < count )
						integer_of ( args [ next++ ], taken );

					precision = static_cast < int32_t > ( base::max < int64_t > ( 0, base::min < int64_t > ( taken, 10000 ) ) );
					i++;

				} else {

					while ( i < pattern.size && pattern.bytes [ i ] >= '0' && pattern.bytes [ i ] <= '9' )
						precision = base::min < int32_t > ( precision * 10 + ( pattern.bytes [ i++ ] - '0' ), 1000000 );

				}

			}

			while ( i < pattern.size && ( pattern.bytes [ i ] == 'l' || pattern.bytes [ i ] == 'h' ) )
				i++;

			if ( i >= pattern.size )
				break;

			char kind = static_cast < char > ( pattern.bytes [ i ] );

			if ( kind == '%' ) {

				made.add ( '%' );

				continue;

			}

			storage::value argument = next < count ? args [ next++ ] : storage::null_value ( );
			base::text body;
			bool numeric = false;

			switch ( kind ) {

				case 'd':
				case 'i':
				case 'u': {

					int64_t number = 0;

					integer_of ( argument, number );

					if ( number < 0 )
						body.add ( '-' ).number ( static_cast < uint64_t > ( -( number + 1 ) ) + 1 );
					else {

						if ( plus )
							body.add ( '+' );
						else if ( space )
							body.add ( ' ' );

						body.number ( static_cast < uint64_t > ( number ) );

					}

					numeric = true;
					break;

				}

				case 'f':
				case 'F': {

					double number = 0;

					real_of ( argument, number );

					if ( number >= 0 && plus )
						body.add ( '+' );
					else if ( number >= 0 && space )
						body.add ( ' ' );

					add_fixed ( body, number, precision < 0 ? 6 : static_cast < uint32_t > ( precision ) );
					numeric = true;
					break;

				}

				case 'e':
				case 'E':
				case 'g':
				case 'G': {

					double number = 0;

					real_of ( argument, number );

					if ( number >= 0 && plus )
						body.add ( '+' );
					else if ( number >= 0 && space )
						body.add ( ' ' );

					if ( kind == 'e' || kind == 'E' )
						add_scientific ( body, number, precision < 0 ? 6 : static_cast < uint32_t > ( base::min < int32_t > ( precision, 16 ) ), kind == 'E' );
					else
						add_general ( body, number, precision, kind == 'G', alternate );

					numeric = true;
					break;

				}

				case 'x':
				case 'X':
				case 'o': {

					int64_t number = 0;
					uint64_t bits = 0;
					char digits [ 24 ];
					uint32_t used = 0;

					integer_of ( argument, number );
					bits = static_cast < uint64_t > ( number );

					do {

						uint32_t digit = kind == 'o' ? bits & 7 : bits & 15;

						digits [ used++ ] = static_cast < char > ( digit < 10 ? '0' + digit : ( kind == 'X' ? 'A' : 'a' ) + digit - 10 );
						bits = kind == 'o' ? bits >> 3 : bits >> 4;

					} while ( bits );

					while ( used )
						body.add ( digits [ --used ] );

					numeric = true;
					break;

				}

				case 'c': {

					storage::value text = text_of ( *ctx.scratch, argument );

					if ( text.size ) {

						size_t at = 0;
						uint32_t code = base::next_code ( text.bytes, text.size, at );
						int32_t times = precision > 0 ? base::min < int32_t > ( precision, 1000000 ) : 1;

						for ( int32_t k = 0; k < times; k++ )
							body.code ( code );

					}

					break;

				}

				case 'q':
				case 'Q':
				case 'w': {

					if ( argument.kind == storage::VALUE_NULL && kind == 'Q' ) {

						body.add ( "NULL" );

						break;

					}

					storage::value text = text_of ( *ctx.scratch, argument );
					char quote = kind == 'w' ? '"' : '\'';

					if ( kind == 'Q' )
						body.add ( quote );

					for ( size_t k = 0; k < text.size; k++ ) {

						if ( text.bytes [ k ] == static_cast < uint8_t > ( quote ) )
							body.add ( quote );

						body.add ( static_cast < char > ( text.bytes [ k ] ) );

					}

					if ( kind == 'Q' )
						body.add ( quote );

					break;

				}

				default: {

					if ( argument.kind == storage::VALUE_NULL )
						break;

					storage::value text = text_of ( *ctx.scratch, argument );
					size_t size = text.size;

					if ( precision >= 0 )
						size = base::skip_codes ( text.bytes, text.size, static_cast < size_t > ( precision ) );

					body.add ( reinterpret_cast < const char* > ( text.bytes ), size );
					break;

				}

			}

			size_t shown = base::count_codes ( reinterpret_cast < const uint8_t* > ( body.value ( ) ), body.size );

			if ( width > shown && !left ) {

				if ( zero && numeric ) {

					size_t sign = body.size && ( body.data [ 0 ] == '-' || body.data [ 0 ] == '+' || body.data [ 0 ] == ' ' ) ? 1 : 0;

					made.add ( body.value ( ), sign ).repeat ( '0', width - shown ).add ( body.value ( ) + sign, body.size - sign );

				} else
					made.repeat ( ' ', width - shown ).add ( body.value ( ), body.size );

			} else {

				made.add ( body.value ( ), body.size );

				if ( width > shown )
					made.repeat ( ' ', width - shown );

			}

		}

		return make_text ( ctx, made, out );

	}

	bool call_function ( run_context& ctx, const sql::expression* item, const storage::value* args, uint32_t count, storage::value& out ) {

		out = storage::null_value ( );

		switch ( static_cast < function_kind > ( item->column ) ) {

			case FUNCTION_ABS: {

				if ( args [ 0 ].kind == storage::VALUE_NULL )
					return true;

				storage::value number = numeric_of ( args [ 0 ] );

				if ( number.kind == storage::VALUE_DECIMAL ) {

					__int128 mantissa = storage::decimal_mantissa ( number );

					out = storage::decimal_raw ( mantissa < 0 ? -mantissa : mantissa, number.subtype );

					return true;

				}

				if ( number.kind == storage::VALUE_INTEGER ) {

					if ( number.integer == static_cast < int64_t > ( 0x8000000000000000ull ) )
						return fail_run ( ctx, "integer overflow in abs()" );

					out = storage::integer_value ( number.integer < 0 ? -number.integer : number.integer );

				} else
					out = storage::real_value ( number.real < 0 ? -number.real : number.real );

				return true;

			}

			case FUNCTION_CHANGES:
				out = storage::integer_value ( ctx.previous_changes );

				return true;

			case FUNCTION_TOTAL_CHANGES:
				out = storage::integer_value ( ctx.total_changes + ctx.changes );

				return true;

			case FUNCTION_LAST_ROWID:
				out = storage::integer_value ( ctx.last_rowid );

				return true;

			case FUNCTION_VERSION:
				out = storage::text_value ( ENGINE_VERSION, sizeof ( ENGINE_VERSION ) - 1 );

				return true;

			case FUNCTION_CHAR: {

				base::text made;

				for ( uint32_t i = 0; i < count; i++ ) {

					int64_t code = 0;

					if ( integer_of ( args [ i ], code ) )
						made.code ( code < 0 || code > 0x10FFFF ? 0xFFFD : static_cast < uint32_t > ( code ) );

				}

				return make_text ( ctx, made, out );

			}

			case FUNCTION_COALESCE:
				for ( uint32_t i = 0; i < count; i++ ) {

					if ( args [ i ].kind != storage::VALUE_NULL ) {

						out = args [ i ];

						break;

					}

				}

				return true;

			case FUNCTION_IFNULL:
				out = args [ 0 ].kind != storage::VALUE_NULL ? args [ 0 ] : args [ 1 ];

				return true;

			case FUNCTION_IIF:
				out = truth_of ( args [ 0 ] ) == TRUTH_TRUE ? args [ 1 ] : args [ 2 ];

				return true;

			case FUNCTION_NULLIF:
				out = compare_with ( sql::OPERATOR_EQUAL, args [ 0 ], args [ 1 ] ) == TRUTH_TRUE ? storage::null_value ( ) : args [ 0 ];

				return true;

			case FUNCTION_CONCAT:
			case FUNCTION_CONCAT_WS: {

				bool separated = static_cast < function_kind > ( item->column ) == FUNCTION_CONCAT_WS;

				if ( separated && args [ 0 ].kind == storage::VALUE_NULL )
					return true;

				storage::value separator = separated ? text_of ( *ctx.scratch, args [ 0 ] ) : storage::null_value ( );
				base::text made;
				bool any = false;

				for ( uint32_t i = separated ? 1 : 0; i < count; i++ ) {

					if ( args [ i ].kind == storage::VALUE_NULL )
						continue;

					storage::value text = text_of ( *ctx.scratch, args [ i ] );

					if ( separated && any )
						made.add ( reinterpret_cast < const char* > ( separator.bytes ), separator.size );

					made.add ( reinterpret_cast < const char* > ( text.bytes ), text.size );
					any = true;

				}

				return make_text ( ctx, made, out );

			}

			case FUNCTION_GLOB:
			case FUNCTION_LIKE: {

				bool glob = static_cast < function_kind > ( item->column ) == FUNCTION_GLOB;

				for ( uint32_t i = 0; i < count; i++ )
					if ( args [ i ].kind == storage::VALUE_NULL )
						return true;

				storage::value pattern = text_of ( *ctx.scratch, args [ 0 ] );
				storage::value text = text_of ( *ctx.scratch, args [ 1 ] );
				uint32_t escape = 0;

				if ( count > 2 ) {

					storage::value written = text_of ( *ctx.scratch, args [ 2 ] );
					size_t at = 0;

					if ( !written.size || base::skip_codes ( written.bytes, written.size, 1 ) != written.size )
						return fail_run ( ctx, "ESCAPE needs exactly one character" );

					escape = base::next_code ( written.bytes, written.size, at );

				}

				out = storage::integer_value ( like_match ( pattern.bytes, pattern.size, text.bytes, text.size, escape, glob ) ? 1 : 0 );

				return true;

			}

			case FUNCTION_HEX: {

				storage::value text = args [ 0 ].kind == storage::VALUE_NULL ? storage::text_value ( "", 0 ) : text_of ( *ctx.scratch, args [ 0 ] );
				base::text made;

				for ( size_t i = 0; i < text.size; i++ )
					made.hex ( text.bytes [ i ], 2, true );

				return make_text ( ctx, made, out );

			}

			case FUNCTION_INSTR: {

				if ( args [ 0 ].kind == storage::VALUE_NULL || args [ 1 ].kind == storage::VALUE_NULL )
					return true;

				bool blob = args [ 0 ].kind == storage::VALUE_BLOB && args [ 1 ].kind == storage::VALUE_BLOB;
				storage::value hay = text_of ( *ctx.scratch, args [ 0 ] );
				storage::value needle = text_of ( *ctx.scratch, args [ 1 ] );
				int64_t found = 0;

				if ( !needle.size ) {

					out = storage::integer_value ( 1 );

					return true;

				}

				for ( size_t i = 0; i + needle.size <= hay.size; i++ ) {

					if ( base::same_bytes ( hay.bytes + i, needle.bytes, needle.size ) ) {

						found = static_cast < int64_t > ( blob ? i : base::count_codes ( hay.bytes, i ) ) + 1;

						break;

					}

				}

				out = storage::integer_value ( found );

				return true;

			}

			case FUNCTION_LENGTH:
			case FUNCTION_OCTET_LENGTH: {

				if ( args [ 0 ].kind == storage::VALUE_NULL )
					return true;

				if ( args [ 0 ].kind == storage::VALUE_BLOB ) {

					out = storage::integer_value ( static_cast < int64_t > ( args [ 0 ].size ) );

					return true;

				}

				storage::value text = text_of ( *ctx.scratch, args [ 0 ] );
				bool bytes = static_cast < function_kind > ( item->column ) == FUNCTION_OCTET_LENGTH;

				out = storage::integer_value ( static_cast < int64_t > ( bytes ? text.size : base::count_codes ( text.bytes, text.size ) ) );

				return true;

			}

			case FUNCTION_LOWER:
			case FUNCTION_UPPER: {

				if ( args [ 0 ].kind == storage::VALUE_NULL )
					return true;

				storage::value text = text_of ( *ctx.scratch, args [ 0 ] );
				char* made = ctx.scratch->make < char > ( text.size + 1 );
				bool upper = static_cast < function_kind > ( item->column ) == FUNCTION_UPPER;

				if ( !made )
					return fail_run ( ctx, "out of memory" );

				for ( size_t i = 0; i < text.size; i++ )
					made [ i ] = upper ? base::upper ( static_cast < char > ( text.bytes [ i ] ) ) : base::lower ( static_cast < char > ( text.bytes [ i ] ) );

				out = storage::text_value ( made, text.size );

				return true;

			}

			case FUNCTION_TRIM:
				return trim_text ( ctx, args, count, true, true, out );

			case FUNCTION_LTRIM:
				return trim_text ( ctx, args, count, true, false, out );

			case FUNCTION_RTRIM:
				return trim_text ( ctx, args, count, false, true, out );

			case FUNCTION_MAX:
			case FUNCTION_MIN: {

				bool lowest = static_cast < function_kind > ( item->column ) == FUNCTION_MIN;

				for ( uint32_t i = 0; i < count; i++ )
					if ( args [ i ].kind == storage::VALUE_NULL )
						return true;

				out = args [ 0 ];

				for ( uint32_t i = 1; i < count; i++ ) {

					int32_t order = order_values ( args [ i ], out );

					if ( lowest ? order < 0 : order > 0 )
						out = args [ i ];

				}

				return true;

			}

			case FUNCTION_PRINTF:
				return format_text ( ctx, args, count, out );

			case FUNCTION_QUOTE: {

				base::text made;

				add_quoted ( made, args [ 0 ] );

				return make_text ( ctx, made, out );

			}

			case FUNCTION_RANDOM:
				out = storage::integer_value ( static_cast < int64_t > ( next_random ( ctx ) ) );

				return true;

			case FUNCTION_SERVER_VERSION: {

				base::text made;

				made.add ( "PostgreSQL 17.0 (Strata " ).add ( ENGINE_VERSION ).add ( ") on x86_64" );

				return make_text ( ctx, made, out );

			}

			case FUNCTION_CURRENT_DATABASE:
			case FUNCTION_CURRENT_SCHEMA:
			case FUNCTION_CURRENT_USER: {

				function_kind kind = static_cast < function_kind > ( item->column );
				const char* value = kind == FUNCTION_CURRENT_DATABASE ? ctx.schema->session_database : kind == FUNCTION_CURRENT_USER ? ctx.schema->session_user : ctx.schema->search;
				base::text made;

				made.add ( value [ 0 ] ? value : kind == FUNCTION_CURRENT_DATABASE ? "strata" : kind == FUNCTION_CURRENT_USER ? "strata" : "public" );

				return make_text ( ctx, made, out );

			}

			case FUNCTION_BACKEND_PID:
				out = storage::integer_value ( ctx.schema->session_id );

				return true;

			case FUNCTION_CURRENT_SETTING: {

				storage::value name = text_of ( *ctx.scratch, args [ 0 ] );
				base::text wanted;
				base::text made;

				wanted.add ( reinterpret_cast < const char* > ( name.bytes ), name.size );
				setting_value ( *ctx.schema, wanted.value ( ), made );

				return make_text ( ctx, made, out );

			}

			case FUNCTION_RANDOM_UUID: {

				static const char digits [ ] = "0123456789abcdef";
				uint8_t bytes [ 16 ];
				base::text made;

				for ( uint32_t i = 0; i < 16; i++ )
					bytes [ i ] = static_cast < uint8_t > ( next_random ( ctx ) >> 24 );

				bytes [ 6 ] = static_cast < uint8_t > ( ( bytes [ 6 ] & 0x0F ) | 0x40 );
				bytes [ 8 ] = static_cast < uint8_t > ( ( bytes [ 8 ] & 0x3F ) | 0x80 );

				for ( uint32_t i = 0; i < 16; i++ ) {

					if ( i == 4 || i == 6 || i == 8 || i == 10 )
						made.add ( '-' );

					made.add ( digits [ bytes [ i ] >> 4 ] ).add ( digits [ bytes [ i ] & 15 ] );

				}

				return make_text ( ctx, made, out );

			}

			case FUNCTION_NOTHING:
				return true;

			case FUNCTION_ARRAY_ANY:
				return array_contains ( ctx, args [ 0 ], args [ 1 ], out );

			case FUNCTION_DATE_PART:
			case FUNCTION_DATE_TRUNC: {

				if ( args [ 0 ].kind == storage::VALUE_NULL || args [ 1 ].kind == storage::VALUE_NULL )
					return true;

				storage::value field = text_of ( *ctx.scratch, args [ 0 ] );
				storage::value moment = text_of ( *ctx.scratch, args [ 1 ] );
				moment_parts parts = { };

				if ( !parse_moment ( moment.bytes, moment.size, false, parts ) )
					return fail_run ( ctx, "invalid input syntax for type timestamp" );

				if ( static_cast < function_kind > ( item->column ) == FUNCTION_DATE_PART ) {

					double number = 0;

					if ( !moment_field ( reinterpret_cast < const char* > ( field.bytes ), field.size, parts, number ) )
						return fail_run ( ctx, "unit is not recognized by date_part" );

					out = storage::real_value ( number );

					return true;

				}

				if ( !truncate_moment ( reinterpret_cast < const char* > ( field.bytes ), field.size, parts ) )
					return fail_run ( ctx, "unit is not recognized by date_trunc" );

				base::text made;

				write_moment ( parts, parts.has_date ? MOMENT_TIMESTAMP : MOMENT_TIME, made );

				return make_text ( ctx, made, out );

			}

			case FUNCTION_RANDOMBLOB:
			case FUNCTION_ZEROBLOB: {

				int64_t size = 0;

				integer_of ( args [ 0 ], size );
				size = base::max < int64_t > ( size, static_cast < function_kind > ( item->column ) == FUNCTION_RANDOMBLOB ? 1 : 0 );

				if ( size > 100000000 )
					return fail_run ( ctx, "the blob would be larger than 100 MB" );

				uint8_t* made = ctx.scratch->make < uint8_t > ( static_cast < size_t > ( size ) + 1 );

				if ( !made )
					return fail_run ( ctx, "out of memory" );

				if ( static_cast < function_kind > ( item->column ) == FUNCTION_RANDOMBLOB )
					for ( int64_t i = 0; i < size; i++ )
						made [ i ] = static_cast < uint8_t > ( next_random ( ctx ) >> 24 );

				out = storage::blob_value ( made, static_cast < size_t > ( size ) );

				return true;

			}

			case FUNCTION_REPLACE: {

				for ( uint32_t i = 0; i < 3; i++ )
					if ( args [ i ].kind == storage::VALUE_NULL )
						return true;

				storage::value text = text_of ( *ctx.scratch, args [ 0 ] );
				storage::value from = text_of ( *ctx.scratch, args [ 1 ] );
				storage::value to = text_of ( *ctx.scratch, args [ 2 ] );

				if ( !from.size ) {

					out = text;

					return true;

				}

				base::text made;

				for ( size_t i = 0; i < text.size; ) {

					if ( i + from.size <= text.size && base::same_bytes ( text.bytes + i, from.bytes, from.size ) ) {

						made.add ( reinterpret_cast < const char* > ( to.bytes ), to.size );
						i += from.size;

					} else
						made.add ( static_cast < char > ( text.bytes [ i++ ] ) );

				}

				return make_text ( ctx, made, out );

			}

			case FUNCTION_ROUND: {

				if ( args [ 0 ].kind == storage::VALUE_NULL || ( count > 1 && args [ 1 ].kind == storage::VALUE_NULL ) )
					return true;

				double number = 0;
				int64_t places = 0;

				real_of ( args [ 0 ], number );

				if ( count > 1 )
					integer_of ( args [ 1 ], places );

				if ( args [ 0 ].kind == storage::VALUE_DECIMAL ) {

					decimal_number exact = { };

					decimal_of ( args [ 0 ], exact );

					if ( decimal_round ( exact, static_cast < int32_t > ( base::max < int64_t > ( -36, base::min < int64_t > ( places, 30 ) ) ) ) ) {

						out = decimal_result ( exact );

						return true;

					}

				}

				places = base::max < int64_t > ( 0, base::min < int64_t > ( places, 30 ) );

				if ( number < 4503599627370496.0 && number > -4503599627370496.0 ) {

					double scale = 1;

					for ( int64_t i = 0; i < places; i++ )
						scale *= 10;

					double shifted = number < 0 ? -number * scale : number * scale;

					if ( shifted < 9.0e18 ) {

						double whole = static_cast < double > ( static_cast < uint64_t > ( shifted + 0.5 ) );

						number = number < 0 ? -whole / scale : whole / scale;

					}

				}

				out = storage::real_value ( number );

				return true;

			}

			case FUNCTION_SIGN: {

				if ( args [ 0 ].kind == storage::VALUE_NULL || args [ 0 ].kind == storage::VALUE_BLOB )
					return true;

				storage::value number = args [ 0 ];

				if ( number.kind == storage::VALUE_TEXT && !read_number_text ( number.bytes, number.size, number, true ) )
					return true;

				double real = 0;

				real_of ( number, real );
				out = storage::integer_value ( real > 0 ? 1 : real < 0 ? -1 : 0 );

				return true;

			}

			case FUNCTION_SUBSTR:
				return substring ( ctx, args, count, out );

			case FUNCTION_TYPEOF: {

				static const char* const names [ 6 ] = { "null", "integer", "real", "text", "blob", "numeric" };
				const char* name = names [ args [ 0 ].kind ];

				out = storage::text_value ( name, base::length ( name ) );

				return true;

			}

			case FUNCTION_UNICODE: {

				if ( args [ 0 ].kind == storage::VALUE_NULL )
					return true;

				storage::value text = text_of ( *ctx.scratch, args [ 0 ] );
				size_t at = 0;

				if ( text.size )
					out = storage::integer_value ( base::next_code ( text.bytes, text.size, at ) );

				return true;

			}

			case FUNCTION_NOW:
			case FUNCTION_DATE:
			case FUNCTION_TIME:
			case FUNCTION_DATETIME:
			case FUNCTION_JULIANDAY:
			case FUNCTION_UNIXEPOCH:
			case FUNCTION_STRFTIME:
				return call_date_function ( ctx, static_cast < function_kind > ( item->column ), args, count, out );

			case FUNCTION_JSON:
			case FUNCTION_JSON_VALID:
			case FUNCTION_JSON_EXTRACT:
			case FUNCTION_JSON_TYPE:
			case FUNCTION_JSON_ARRAY:
			case FUNCTION_JSON_OBJECT:
			case FUNCTION_JSON_ARRAY_LENGTH:
			case FUNCTION_JSON_QUOTE:
				return call_json_function ( ctx, static_cast < function_kind > ( item->column ), args, count, out );

			default:
				return fail_named ( ctx, "the function ", item->name, "() cannot be called here" );

		}

	}

}
