#include "server/server.hh"

#define PG_EPOCH_DAYS 10957

namespace server {

	static bool type_word ( const char* type, const char* word ) {

		size_t size = base::length ( word );

		for ( const char* at = type; *at; at++ ) {

			bool hit = true;

			for ( size_t i = 0; i < size && hit; i++ )
				hit = at [ i ] && base::lower ( at [ i ] ) == word [ i ];

			if ( hit )
				return true;

		}

		return false;

	}

	uint32_t declared_type ( const char* type ) {

		if ( !type || !*type )
			return 0;

		if ( type_word ( type, "bool" ) )
			return PG_BOOL;

		if ( type_word ( type, "bigint" ) || type_word ( type, "int8" ) || type_word ( type, "bigserial" ) )
			return PG_INT8;

		if ( type_word ( type, "smallint" ) || type_word ( type, "int2" ) )
			return PG_INT2;

		if ( type_word ( type, "int" ) || type_word ( type, "serial" ) )
			return PG_INT4;

		if ( type_word ( type, "timestamptz" ) || type_word ( type, "with time zone" ) )
			return PG_TIMESTAMPTZ;

		if ( type_word ( type, "timestamp" ) || type_word ( type, "datetime" ) )
			return PG_TIMESTAMP;

		if ( type_word ( type, "date" ) )
			return PG_DATE;

		if ( type_word ( type, "time" ) )
			return PG_TIME;

		if ( type_word ( type, "uuid" ) )
			return PG_UUID;

		if ( type_word ( type, "jsonb" ) )
			return PG_JSONB;

		if ( type_word ( type, "json" ) )
			return PG_JSON;

		if ( type_word ( type, "bytea" ) || type_word ( type, "blob" ) )
			return PG_BYTEA;

		if ( type_word ( type, "double" ) || type_word ( type, "float8" ) || type_word ( type, "float" ) )
			return PG_FLOAT8;

		if ( type_word ( type, "real" ) || type_word ( type, "float4" ) )
			return PG_FLOAT4;

		if ( type_word ( type, "numeric" ) || type_word ( type, "decimal" ) || type_word ( type, "money" ) )
			return PG_NUMERIC;

		if ( type_word ( type, "char" ) || type_word ( type, "text" ) || type_word ( type, "clob" ) )
			return PG_TEXT;

		return 0;

	}

	static uint32_t expression_type ( const engine::prepared_select* plan, const sql::expression* item ) {

		if ( !item )
			return PG_TEXT;

		switch ( item->kind ) {

			case sql::EXPRESSION_LITERAL:
				return item->literal.kind == storage::VALUE_INTEGER ? PG_INT8 : item->literal.kind == storage::VALUE_REAL ? PG_FLOAT8 : item->literal.kind == storage::VALUE_BLOB ? PG_BYTEA : PG_TEXT;

			case sql::EXPRESSION_COLUMN: {

				if ( item->depth == 0 && item->source >= 0 && plan && static_cast < uint32_t > ( item->source ) < plan->source_count ) {

					const engine::prepared_source& source = plan->sources [ item->source ];

					if ( source.table && item->column >= 0 ) {

						uint32_t declared = declared_type ( source.table->columns [ item->column ].type );

						if ( declared )
							return declared;

					} else if ( source.table )
						return PG_INT8;

				}

				switch ( item->cast ) {

					case sql::AFFINITY_INTEGER: return PG_INT8;
					case sql::AFFINITY_REAL: return PG_FLOAT8;
					case sql::AFFINITY_NUMERIC: return PG_NUMERIC;
					default: return PG_TEXT;

				}

			}

			case sql::EXPRESSION_CAST: {

				uint32_t declared = declared_type ( item->name );

				return declared ? declared : PG_TEXT;

			}

			case sql::EXPRESSION_IN:
			case sql::EXPRESSION_BETWEEN:
			case sql::EXPRESSION_NULL_TEST:
			case sql::EXPRESSION_LIKE:
			case sql::EXPRESSION_EXISTS:
				return PG_BOOL;

			case sql::EXPRESSION_UNARY:
				return item->op == sql::OPERATOR_NOT ? PG_BOOL : expression_type ( plan, item->left );

			case sql::EXPRESSION_BINARY:
				switch ( item->op ) {

					case sql::OPERATOR_EQUAL:
					case sql::OPERATOR_NOT_EQUAL:
					case sql::OPERATOR_LESS:
					case sql::OPERATOR_LESS_EQUAL:
					case sql::OPERATOR_GREATER:
					case sql::OPERATOR_GREATER_EQUAL:
					case sql::OPERATOR_IS:
					case sql::OPERATOR_IS_NOT:
					case sql::OPERATOR_AND:
					case sql::OPERATOR_OR:
						return PG_BOOL;

					case sql::OPERATOR_CONCAT:
						return PG_TEXT;

					default: {

						uint32_t left = expression_type ( plan, item->left );
						uint32_t right = expression_type ( plan, item->right );

						if ( left == PG_FLOAT8 || right == PG_FLOAT8 || left == PG_FLOAT4 || right == PG_FLOAT4 || ( item->op == sql::OPERATOR_DIVIDE && ( left == PG_NUMERIC || right == PG_NUMERIC ) ) )
							return PG_FLOAT8;

						if ( left == PG_NUMERIC || right == PG_NUMERIC )
							return PG_NUMERIC;

						return PG_INT8;

					}

				}

			case sql::EXPRESSION_FUNCTION: {

				const char* name = item->name;

				if ( base::same_folded ( name, "count" ) || base::same_folded ( name, "length" ) || base::same_folded ( name, "instr" ) || base::same_folded ( name, "row_number" ) || base::same_folded ( name, "rank" ) || base::same_folded ( name, "dense_rank" ) || base::same_folded ( name, "ntile" ) || base::same_folded ( name, "unixepoch" ) || base::same_folded ( name, "changes" ) || base::same_folded ( name, "last_insert_rowid" ) || base::same_folded ( name, "random" ) || base::same_folded ( name, "sign" ) )
					return PG_INT8;

				if ( base::same_folded ( name, "avg" ) || base::same_folded ( name, "total" ) || base::same_folded ( name, "julianday" ) || base::same_folded ( name, "round" ) || base::same_folded ( name, "percent_rank" ) || base::same_folded ( name, "cume_dist" ) || base::same_folded ( name, "bm25" ) )
					return PG_FLOAT8;

				if ( base::same_folded ( name, "datetime" ) )
					return PG_TIMESTAMP;

				if ( base::same_folded ( name, "date" ) )
					return PG_DATE;

				if ( base::same_folded ( name, "json" ) || base::same_folded ( name, "json_object" ) || base::same_folded ( name, "json_array" ) || base::same_folded ( name, "json_group_array" ) || base::same_folded ( name, "json_group_object" ) )
					return PG_JSON;

				if ( ( base::same_folded ( name, "sum" ) || base::same_folded ( name, "min" ) || base::same_folded ( name, "max" ) || base::same_folded ( name, "abs" ) || base::same_folded ( name, "coalesce" ) || base::same_folded ( name, "ifnull" ) || base::same_folded ( name, "first_value" ) || base::same_folded ( name, "last_value" ) || base::same_folded ( name, "lag" ) || base::same_folded ( name, "lead" ) ) && item->count )
					return expression_type ( plan, item->list [ 0 ] );

				return PG_TEXT;

			}

			case sql::EXPRESSION_CASE:
				return item->count >= 2 ? expression_type ( plan, item->list [ 1 ] ) : PG_TEXT;

			case sql::EXPRESSION_SUBQUERY: {

				const engine::prepared_select* child = item->query ? static_cast < const engine::prepared_select* > ( item->query->prepared ) : nullptr;

				return child && child->output_count ? expression_type ( child, child->outputs [ 0 ] ) : PG_TEXT;

			}

			default:
				return PG_TEXT;

		}

	}

	uint32_t column_type ( const engine::prepared_select* plan, uint32_t column ) {

		if ( !plan || column >= plan->output_count )
			return PG_TEXT;

		return expression_type ( plan, plan->outputs [ column ] );

	}

	static int64_t days_from_civil ( int64_t year, int64_t month, int64_t day ) {

		year -= month <= 2;

		int64_t era = ( year >= 0 ? year : year - 399 ) / 400;
		int64_t of_era = year - era * 400;
		int64_t of_year = ( 153 * ( month + ( month > 2 ? -3 : 9 ) ) + 2 ) / 5 + day - 1;
		int64_t of_cycle = of_era * 365 + of_era / 4 - of_era / 100 + of_year;

		return era * 146097 + of_cycle - 719468;

	}

	static void civil_from_days ( int64_t days, int64_t& year, int64_t& month, int64_t& day ) {

		days += 719468;

		int64_t era = ( days >= 0 ? days : days - 146096 ) / 146097;
		int64_t of_era = days - era * 146097;
		int64_t of_cycle = ( of_era - of_era / 1460 + of_era / 36524 - of_era / 146096 ) / 365;
		int64_t of_year = of_era - ( 365 * of_cycle + of_cycle / 4 - of_cycle / 100 );
		int64_t point = ( 5 * of_year + 2 ) / 153;

		day = of_year - ( 153 * point + 2 ) / 5 + 1;
		month = point + ( point < 10 ? 3 : -9 );
		year = of_cycle + era * 400 + ( month <= 2 );

	}

	static bool digits ( const uint8_t*& at, const uint8_t* end, uint32_t count, int64_t& out ) {

		out = 0;

		for ( uint32_t i = 0; i < count; i++ ) {

			if ( at >= end || *at < '0' || *at > '9' )
				return false;

			out = out * 10 + ( *at++ - '0' );

		}

		return true;

	}

	static bool read_moment ( const storage::value& item, int64_t& days, int64_t& micros, bool& has_time ) {

		if ( item.kind != storage::VALUE_TEXT )
			return false;

		const uint8_t* at = item.bytes;
		const uint8_t* end = item.bytes + item.size;
		int64_t year = 0;
		int64_t month = 0;
		int64_t day = 0;

		days = 0;
		micros = 0;
		has_time = false;

		if ( !digits ( at, end, 4, year ) || at >= end || *at++ != '-' || !digits ( at, end, 2, month ) || at >= end || *at++ != '-' || !digits ( at, end, 2, day ) )
			return false;

		days = days_from_civil ( year, month, day ) - PG_EPOCH_DAYS;

		if ( at < end && ( *at == ' ' || *at == 'T' ) ) {

			int64_t hour = 0;
			int64_t minute = 0;
			int64_t second = 0;

			at++;

			if ( !digits ( at, end, 2, hour ) || at >= end || *at++ != ':' || !digits ( at, end, 2, minute ) )
				return false;

			if ( at < end && *at == ':' ) {

				at++;

				if ( !digits ( at, end, 2, second ) )
					return false;

			}

			micros = ( hour * 3600 + minute * 60 + second ) * 1000000;

			if ( at < end && *at == '.' ) {

				int64_t scale = 100000;

				at++;

				while ( at < end && *at >= '0' && *at <= '9' ) {

					micros += ( *at++ - '0' ) * scale;
					scale /= 10;

				}

			}

			while ( at < end && *at == ' ' )
				at++;

			if ( at < end && ( *at == '+' || *at == '-' ) ) {

				bool behind = *at++ == '-';
				int64_t zone_hour = 0;
				int64_t zone_minute = 0;

				digits ( at, end, 2, zone_hour );

				if ( at < end && *at == ':' )
					at++;

				digits ( at, end, 2, zone_minute );

				int64_t shift = ( zone_hour * 3600 + zone_minute * 60 ) * 1000000;

				micros += behind ? shift : -shift;

			}

			has_time = true;

		}

		return true;

	}

	static void two ( base::text& out, int64_t value ) {

		if ( value < 10 )
			out.add ( '0' );

		out.number ( static_cast < uint64_t > ( value ) );

	}

	static void add_civil ( base::text& out, int64_t days ) {

		int64_t year = 0;
		int64_t month = 0;
		int64_t day = 0;

		civil_from_days ( days + PG_EPOCH_DAYS, year, month, day );
		out.number ( static_cast < uint64_t > ( year ) ).add ( '-' );
		two ( out, month );
		out.add ( '-' );
		two ( out, day );

	}

	static void add_clock ( base::text& out, int64_t micros ) {

		int64_t seconds = micros / 1000000;
		int64_t fraction = micros % 1000000;

		two ( out, seconds / 3600 );
		out.add ( ':' );
		two ( out, seconds / 60 % 60 );
		out.add ( ':' );
		two ( out, seconds % 60 );

		if ( fraction ) {

			char written [ 7 ];

			for ( int32_t i = 5; i >= 0; i-- ) {

				written [ i ] = static_cast < char > ( '0' + fraction % 10 );
				fraction /= 10;

			}

			int32_t used = 6;

			while ( used > 1 && written [ used - 1 ] == '0' )
				used--;

			out.add ( '.' ).add ( written, static_cast < size_t > ( used ) );

		}

	}

	static void put_length ( base::text& out, int32_t size ) {

		char bytes [ 4 ] = { static_cast < char > ( size >> 24 ), static_cast < char > ( size >> 16 ), static_cast < char > ( size >> 8 ), static_cast < char > ( size ) };

		out.add ( bytes, 4 );

	}

	static void put_big ( base::text& out, uint64_t value, uint32_t size ) {

		for ( uint32_t i = size; i-- > 0; )
			out.add ( static_cast < char > ( value >> ( i * 8 ) ) );

	}

	static bool numeric_binary ( base::text& out, const char* text, size_t size ) {

		bool negative = false;
		size_t at = 0;

		if ( at < size && ( text [ at ] == '-' || text [ at ] == '+' ) )
			negative = text [ at++ ] == '-';

		base::text whole;
		base::text fraction;

		while ( at < size && text [ at ] >= '0' && text [ at ] <= '9' )
			whole.add ( text [ at++ ] );

		if ( at < size && text [ at ] == '.' ) {

			at++;

			while ( at < size && text [ at ] >= '0' && text [ at ] <= '9' )
				fraction.add ( text [ at++ ] );

		}

		if ( at != size || ( !whole.size && !fraction.size ) )
			return false;

		uint32_t scale = static_cast < uint32_t > ( fraction.size );
		uint32_t lead = static_cast < uint32_t > ( ( 4 - whole.size % 4 ) % 4 );
		base::text padded;

		padded.repeat ( '0', lead ).add ( whole.value ( ), whole.size ).add ( fraction.value ( ), fraction.size ).repeat ( '0', ( 4 - fraction.size % 4 ) % 4 );

		uint32_t groups = static_cast < uint32_t > ( padded.size / 4 );
		uint32_t whole_groups = static_cast < uint32_t > ( ( whole.size + lead ) / 4 );
		uint16_t values [ 512 ];
		uint32_t count = 0;

		for ( uint32_t g = 0; g < groups && count < 512; g++ ) {

			uint32_t value = 0;

			for ( uint32_t k = 0; k < 4; k++ )
				value = value * 10 + static_cast < uint32_t > ( padded.data [ g * 4 + k ] - '0' );

			values [ count++ ] = static_cast < uint16_t > ( value );

		}

		uint32_t first = 0;
		int32_t weight = static_cast < int32_t > ( whole_groups ) - 1;

		while ( first < count && !values [ first ] ) {

			first++;
			weight--;

		}

		while ( count > first && !values [ count - 1 ] )
			count--;

		uint32_t used = count - first;

		if ( !used )
			weight = 0;

		put_length ( out, static_cast < int32_t > ( 8 + used * 2 ) );
		put_big ( out, used, 2 );
		put_big ( out, static_cast < uint16_t > ( weight ), 2 );
		put_big ( out, negative ? 0x4000 : 0, 2 );
		put_big ( out, scale, 2 );

		for ( uint32_t i = first; i < count; i++ )
			put_big ( out, values [ i ], 2 );

		return true;

	}

	static bool number_value ( const storage::value& item, storage::value& out ) {

		if ( item.kind == storage::VALUE_INTEGER || item.kind == storage::VALUE_REAL ) {

			out = item;

			return true;

		}

		if ( item.kind == storage::VALUE_TEXT )
			return engine::read_number_text ( item.bytes, item.size, out, true );

		return false;

	}

	bool encode_value ( server_connection& link, const storage::value& item, uint32_t type, bool binary ) {

		base::text& out = link.output;

		if ( item.kind == storage::VALUE_NULL ) {

			put_length ( out, -1 );

			return true;

		}

		if ( !binary ) {

			base::text written;

			switch ( type ) {

				case PG_BOOL: {

					storage::value number;
					bool truth = number_value ( item, number ) ? ( number.kind == storage::VALUE_INTEGER ? number.integer != 0 : number.real != 0 ) : item.size && ( item.bytes [ 0 ] == 't' || item.bytes [ 0 ] == 'T' || item.bytes [ 0 ] == 'y' );

					written.add ( truth ? 't' : 'f' );
					break;

				}

				case PG_BYTEA: {

					written.add ( "\\x" );

					for ( size_t i = 0; i < item.size && item.kind != storage::VALUE_INTEGER && item.kind != storage::VALUE_REAL; i++ )
						written.hex ( item.bytes [ i ], 2 );

					break;

				}

				default:
					engine::describe_value ( written, item );

					if ( item.kind == storage::VALUE_BLOB ) {

						written.clear ( );
						written.add ( reinterpret_cast < const char* > ( item.bytes ), item.size );

					}

					break;

			}

			put_length ( out, static_cast < int32_t > ( written.size ) );
			out.add ( written.value ( ), written.size );

			return true;

		}

		storage::value number;

		switch ( type ) {

			case PG_BOOL: {

				bool truth = number_value ( item, number ) ? ( number.kind == storage::VALUE_INTEGER ? number.integer != 0 : number.real != 0 ) : item.size && ( item.bytes [ 0 ] == 't' || item.bytes [ 0 ] == 'T' );

				put_length ( out, 1 );
				out.add ( truth ? '\1' : '\0' );

				return true;

			}

			case PG_INT2:
			case PG_INT4:
			case PG_INT8:
			case PG_OID: {

				int64_t value = 0;

				if ( !number_value ( item, number ) || !engine::integer_of ( number, value ) )
					return false;

				uint32_t size = type == PG_INT2 ? 2 : type == PG_INT8 ? 8 : 4;

				put_length ( out, static_cast < int32_t > ( size ) );
				put_big ( out, static_cast < uint64_t > ( value ), size );

				return true;

			}

			case PG_FLOAT4:
			case PG_FLOAT8: {

				double value = 0;

				if ( !number_value ( item, number ) || !engine::real_of ( number, value ) )
					return false;

				if ( type == PG_FLOAT4 ) {

					float narrow = static_cast < float > ( value );
					uint32_t bits = 0;

					base::copy ( &bits, &narrow, 4 );
					put_length ( out, 4 );
					put_big ( out, bits, 4 );

				} else {

					uint64_t bits = 0;

					base::copy ( &bits, &value, 8 );
					put_length ( out, 8 );
					put_big ( out, bits, 8 );

				}

				return true;

			}

			case PG_NUMERIC: {

				base::text written;

				engine::describe_value ( written, item );

				return numeric_binary ( out, written.value ( ), written.size );

			}

			case PG_UUID: {

				uint8_t bytes [ 16 ];
				uint32_t count = 0;

				for ( size_t i = 0; item.kind == storage::VALUE_TEXT && i < item.size && count < 32; i++ ) {

					char c = base::lower ( static_cast < char > ( item.bytes [ i ] ) );
					uint32_t value = c >= '0' && c <= '9' ? static_cast < uint32_t > ( c - '0' ) : c >= 'a' && c <= 'f' ? static_cast < uint32_t > ( c - 'a' + 10 ) : 99;

					if ( c == '-' || c == '{' || c == '}' )
						continue;

					if ( value == 99 )
						return false;

					if ( count % 2 == 0 )
						bytes [ count / 2 ] = static_cast < uint8_t > ( value << 4 );
					else
						bytes [ count / 2 ] |= static_cast < uint8_t > ( value );

					count++;

				}

				if ( item.kind == storage::VALUE_BLOB && item.size == 16 ) {

					base::copy ( bytes, item.bytes, 16 );
					count = 32;

				}

				if ( count != 32 )
					return false;

				put_length ( out, 16 );
				out.add ( reinterpret_cast < const char* > ( bytes ), 16 );

				return true;

			}

			case PG_DATE:
			case PG_TIMESTAMP:
			case PG_TIMESTAMPTZ: {

				int64_t days = 0;
				int64_t micros = 0;
				bool has_time = false;

				if ( !read_moment ( item, days, micros, has_time ) )
					return false;

				if ( type == PG_DATE ) {

					put_length ( out, 4 );
					put_big ( out, static_cast < uint64_t > ( days ), 4 );

				} else {

					put_length ( out, 8 );
					put_big ( out, static_cast < uint64_t > ( days * 86400000000ll + micros ), 8 );

				}

				return true;

			}

			case PG_JSONB: {

				base::text written;

				engine::describe_value ( written, item );
				put_length ( out, static_cast < int32_t > ( written.size + 1 ) );
				out.add ( '\1' ).add ( written.value ( ), written.size );

				return true;

			}

			default: {

				if ( item.kind == storage::VALUE_TEXT || item.kind == storage::VALUE_BLOB ) {

					put_length ( out, static_cast < int32_t > ( item.size ) );
					out.add ( reinterpret_cast < const char* > ( item.bytes ), item.size );

					return true;

				}

				base::text written;

				engine::describe_value ( written, item );
				put_length ( out, static_cast < int32_t > ( written.size ) );
				out.add ( written.value ( ), written.size );

				return true;

			}

		}

	}

	static uint64_t read_big ( const uint8_t* data, uint32_t size ) {

		uint64_t value = 0;

		for ( uint32_t i = 0; i < size; i++ )
			value = value << 8 | data [ i ];

		return value;

	}

	static bool keep_text ( base::arena& memory, const base::text& made, strata_value& out ) {

		char* kept = memory.keep ( made.value ( ), made.size );

		if ( !kept )
			return false;

		out.type = STRATA_TEXT;
		out.size = static_cast < uint32_t > ( made.size );
		out.data.bytes = kept;

		return true;

	}

	bool decode_parameter ( base::arena& memory, const uint8_t* data, int32_t size, uint32_t type, bool binary, strata_value& out ) {

		out = { };

		if ( size < 0 ) {

			out.type = STRATA_NULL;

			return true;

		}

		if ( !binary ) {

			storage::value number;

			if ( ( type == PG_INT2 || type == PG_INT4 || type == PG_INT8 || type == PG_OID || type == PG_FLOAT4 || type == PG_FLOAT8 ) && engine::read_number_text ( data, static_cast < size_t > ( size ), number, true ) ) {

				if ( number.kind == storage::VALUE_INTEGER ) {

					out.type = STRATA_INTEGER;
					out.data.integer = number.integer;

				} else {

					out.type = STRATA_REAL;
					out.data.real = number.real;

				}

				return true;

			}

			if ( type == PG_BOOL && size > 0 ) {

				out.type = STRATA_INTEGER;
				out.data.integer = data [ 0 ] == 't' || data [ 0 ] == 'T' || data [ 0 ] == '1' || data [ 0 ] == 'y' || ( data [ 0 ] == 'o' && size > 1 && data [ 1 ] == 'n' ) ? 1 : 0;

				return true;

			}

			if ( type == PG_BYTEA && size >= 2 && data [ 0 ] == '\\' && data [ 1 ] == 'x' ) {

				size_t count = static_cast < size_t > ( size - 2 ) / 2;
				uint8_t* bytes = memory.make < uint8_t > ( count + 1 );

				if ( !bytes )
					return false;

				for ( size_t i = 0; i < count; i++ ) {

					uint8_t value = 0;

					for ( uint32_t k = 0; k < 2; k++ ) {

						char c = base::lower ( static_cast < char > ( data [ 2 + i * 2 + k ] ) );

						value = static_cast < uint8_t > ( value << 4 | ( c >= 'a' ? c - 'a' + 10 : c - '0' ) );

					}

					bytes [ i ] = value;

				}

				out.type = STRATA_BLOB;
				out.size = static_cast < uint32_t > ( count );
				out.data.bytes = bytes;

				return true;

			}

			char* kept = memory.keep ( reinterpret_cast < const char* > ( data ), static_cast < size_t > ( size ) );

			if ( !kept )
				return false;

			out.type = STRATA_TEXT;
			out.size = static_cast < uint32_t > ( size );
			out.data.bytes = kept;

			return true;

		}

		switch ( type ) {

			case PG_BOOL:
				out.type = STRATA_INTEGER;
				out.data.integer = size > 0 && data [ 0 ] ? 1 : 0;

				return true;

			case PG_INT2:
			case PG_INT4:
			case PG_INT8:
			case PG_OID: {

				if ( size != 2 && size != 4 && size != 8 )
					return false;

				uint64_t raw = read_big ( data, static_cast < uint32_t > ( size ) );

				out.type = STRATA_INTEGER;
				out.data.integer = size == 2 ? static_cast < int16_t > ( raw ) : size == 4 ? static_cast < int32_t > ( raw ) : static_cast < int64_t > ( raw );

				return true;

			}

			case PG_FLOAT4:
			case PG_FLOAT8: {

				if ( size == 4 ) {

					uint32_t bits = static_cast < uint32_t > ( read_big ( data, 4 ) );
					float narrow = 0;

					base::copy ( &narrow, &bits, 4 );
					out.data.real = narrow;

				} else if ( size == 8 ) {

					uint64_t bits = read_big ( data, 8 );

					base::copy ( &out.data.real, &bits, 8 );

				} else
					return false;

				out.type = STRATA_REAL;

				return true;

			}

			case PG_BYTEA: {

				uint8_t* bytes = memory.make < uint8_t > ( static_cast < size_t > ( size ) + 1 );

				if ( !bytes )
					return false;

				base::copy ( bytes, data, static_cast < size_t > ( size ) );
				out.type = STRATA_BLOB;
				out.size = static_cast < uint32_t > ( size );
				out.data.bytes = bytes;

				return true;

			}

			case PG_UUID: {

				if ( size != 16 )
					return false;

				base::text made;

				for ( uint32_t i = 0; i < 16; i++ ) {

					if ( i == 4 || i == 6 || i == 8 || i == 10 )
						made.add ( '-' );

					made.hex ( data [ i ], 2 );

				}

				return keep_text ( memory, made, out );

			}

			case PG_DATE: {

				if ( size != 4 )
					return false;

				base::text made;

				add_civil ( made, static_cast < int32_t > ( read_big ( data, 4 ) ) );

				return keep_text ( memory, made, out );

			}

			case PG_TIMESTAMP:
			case PG_TIMESTAMPTZ: {

				if ( size != 8 )
					return false;

				int64_t micros = static_cast < int64_t > ( read_big ( data, 8 ) );
				int64_t days = micros >= 0 ? micros / 86400000000ll : ( micros - 86399999999ll ) / 86400000000ll;
				base::text made;

				add_civil ( made, days );
				made.add ( ' ' );
				add_clock ( made, micros - days * 86400000000ll );

				return keep_text ( memory, made, out );

			}

			case PG_TIME: {

				if ( size != 8 )
					return false;

				base::text made;

				add_clock ( made, static_cast < int64_t > ( read_big ( data, 8 ) ) );

				return keep_text ( memory, made, out );

			}

			case PG_NUMERIC: {

				if ( size < 8 )
					return false;

				uint32_t count = static_cast < uint32_t > ( read_big ( data, 2 ) );
				int32_t weight = static_cast < int16_t > ( read_big ( data + 2, 2 ) );
				uint32_t sign = static_cast < uint32_t > ( read_big ( data + 4, 2 ) );
				uint32_t scale = static_cast < uint32_t > ( read_big ( data + 6, 2 ) );
				base::text made;

				if ( static_cast < size_t > ( size ) < 8 + count * 2 )
					return false;

				if ( sign == 0xC000 ) {

					made.add ( "NaN" );

					return keep_text ( memory, made, out );

				}

				if ( sign == 0x4000 )
					made.add ( '-' );

				if ( weight < 0 )
					made.add ( '0' );

				for ( int32_t g = 0; g <= weight; g++ ) {

					uint32_t value = g < static_cast < int32_t > ( count ) ? static_cast < uint32_t > ( read_big ( data + 8 + g * 2, 2 ) ) : 0;

					if ( g == 0 )
						made.number ( value );
					else {

						char four [ 4 ] = { static_cast < char > ( '0' + value / 1000 ), static_cast < char > ( '0' + value / 100 % 10 ), static_cast < char > ( '0' + value / 10 % 10 ), static_cast < char > ( '0' + value % 10 ) };

						made.add ( four, 4 );

					}

				}

				if ( scale ) {

					base::text fraction;

					for ( int32_t g = weight + 1; fraction.size < scale; g++ ) {

						uint32_t value = g >= 0 && g < static_cast < int32_t > ( count ) ? static_cast < uint32_t > ( read_big ( data + 8 + g * 2, 2 ) ) : 0;
						char four [ 4 ] = { static_cast < char > ( '0' + value / 1000 ), static_cast < char > ( '0' + value / 100 % 10 ), static_cast < char > ( '0' + value / 10 % 10 ), static_cast < char > ( '0' + value % 10 ) };

						fraction.add ( four, 4 );

					}

					made.add ( '.' ).add ( fraction.value ( ), scale );

				}

				return keep_text ( memory, made, out );

			}

			case PG_JSONB:
				if ( size >= 1 ) {

					data++;
					size--;

				}

				break;

			default:
				break;

		}

		char* kept = memory.keep ( reinterpret_cast < const char* > ( data ), static_cast < size_t > ( size ) );

		if ( !kept )
			return false;

		out.type = STRATA_TEXT;
		out.size = static_cast < uint32_t > ( size );
		out.data.bytes = kept;

		return true;

	}

	const char* error_code ( const char* message, bool parse_failed ) {

		if ( parse_failed )
			return "42601";

		if ( base::starts_with ( message, "UNIQUE constraint" ) )
			return "23505";

		if ( base::starts_with ( message, "NOT NULL constraint" ) )
			return "23502";

		if ( base::starts_with ( message, "FOREIGN KEY constraint" ) )
			return "23503";

		if ( base::starts_with ( message, "CHECK constraint" ) )
			return "23514";

		if ( base::starts_with ( message, "no such table" ) )
			return "42P01";

		if ( base::starts_with ( message, "no such column" ) )
			return "42703";

		if ( base::starts_with ( message, "no such function" ) )
			return "42883";

		if ( base::starts_with ( message, "the name " ) )
			return "42P07";

		return "XX000";

	}

}
