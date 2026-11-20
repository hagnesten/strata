#include "server/server.hh"
#include "engine/value/interval.hh"

#define PG_EPOCH_DAYS 10957

namespace server {

	static uint32_t array_element ( uint32_t type );

	uint32_t declared_type ( const char* type ) {

		uint32_t oid = engine::postgres_type_of ( type );

		switch ( oid ) {

			case PG_VARCHAR: case PG_BPCHAR:
			case PG_BOOL: case PG_BYTEA: case PG_INT8: case PG_INT2: case PG_INT4: case PG_TEXT: case PG_OID: case PG_JSON: case PG_FLOAT4: case PG_FLOAT8:
			case PG_DATE: case PG_TIME: case PG_TIMESTAMP: case PG_TIMESTAMPTZ: case PG_NUMERIC: case PG_UUID: case PG_JSONB: case PG_INTERVAL:
				return oid;

			case 0:
				return 0;

			default:
				return array_element ( oid ) ? oid : PG_TEXT;

		}

	}

	static uint32_t expression_type ( const engine::prepared_select* plan, const sql::expression* item ) {

		if ( !item )
			return PG_TEXT;

		switch ( item->kind ) {

			case sql::EXPRESSION_LITERAL:
				if ( item->literal.kind == storage::VALUE_INTEGER )
					return item->literal.subtype == LITERAL_BOOLEAN ? PG_BOOL : item->literal.integer >= -2147483648ll && item->literal.integer <= 2147483647ll ? PG_INT4 : PG_INT8;

				return item->literal.kind == storage::VALUE_DECIMAL ? PG_NUMERIC : item->literal.kind == storage::VALUE_REAL ? PG_FLOAT8 : item->literal.kind == storage::VALUE_BLOB ? PG_BYTEA : item->literal.kind == storage::VALUE_NULL ? PG_TEXT : PG_TEXT;

			case sql::EXPRESSION_COLUMN: {

				if ( item->depth == 0 && item->source >= 0 && plan && static_cast < uint32_t > ( item->source ) < plan->source_count ) {

					const engine::prepared_source& source = plan->sources [ item->source ];

					if ( source.table && item->column >= 0 ) {

						uint32_t declared = declared_type ( source.table->columns [ item->column ].type );

						if ( declared )
							return declared;

					} else if ( source.table )
						return PG_INT8;
					else if ( source.query && item->column >= 0 && static_cast < uint32_t > ( item->column ) < source.query->output_count && source.query->outputs [ item->column ] )
						return expression_type ( source.query, source.query->outputs [ item->column ] );

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

						if ( item->op == sql::OPERATOR_JSON )
							return PG_JSONB;

						if ( item->op == sql::OPERATOR_JSON_VALUE )
							return PG_TEXT;

						bool left_moment = left == PG_TIMESTAMP || left == PG_TIMESTAMPTZ || left == PG_DATE;
						bool right_moment = right == PG_TIMESTAMP || right == PG_TIMESTAMPTZ || right == PG_DATE;

						if ( item->op == sql::OPERATOR_ADD || item->op == sql::OPERATOR_SUBTRACT ) {

							if ( left_moment && right == PG_INTERVAL )
								return left == PG_DATE ? PG_TIMESTAMP : left;

							if ( right_moment && left == PG_INTERVAL && item->op == sql::OPERATOR_ADD )
								return right == PG_DATE ? PG_TIMESTAMP : right;

							if ( left_moment && right_moment && item->op == sql::OPERATOR_SUBTRACT )
								return left == PG_DATE && right == PG_DATE ? PG_INT4 : PG_INTERVAL;

							if ( left == PG_DATE && ( right == PG_INT4 || right == PG_INT2 || right == PG_INT8 ) )
								return PG_DATE;

						}

						if ( left == PG_INTERVAL || right == PG_INTERVAL )
							return PG_INTERVAL;

						if ( left == PG_FLOAT8 || right == PG_FLOAT8 || left == PG_FLOAT4 || right == PG_FLOAT4 )
							return PG_FLOAT8;

						if ( left == PG_NUMERIC || right == PG_NUMERIC )
							return PG_NUMERIC;

						if ( ( left == PG_INT4 || left == PG_INT2 ) && ( right == PG_INT4 || right == PG_INT2 ) )
							return PG_INT4;

						return PG_INT8;

					}

				}

			case sql::EXPRESSION_FUNCTION: {

				const char* name = item->name;

				if ( base::same_folded ( name, "strata_any" ) || base::same_folded ( name, "pg_terminate_backend" ) || base::same_folded ( name, "strata_contains" ) || base::same_folded ( name, "strata_overlap" ) )
					return PG_BOOL;

				if ( base::same_folded ( name, "cardinality" ) || base::same_folded ( name, "array_length" ) )
					return PG_INT4;

				if ( base::same_folded ( name, "date_part" ) )
					return PG_FLOAT8;

				if ( base::same_folded ( name, "nextval" ) || base::same_folded ( name, "currval" ) || base::same_folded ( name, "setval" ) )
					return PG_INT8;

				if ( base::same_folded ( name, "pg_get_serial_sequence" ) )
					return PG_TEXT;

				if ( base::same_folded ( name, "make_interval" ) )
					return PG_INTERVAL;

				if ( base::same_folded ( name, "timezone" ) && item->count > 1 )
					return expression_type ( plan, item->list [ 1 ] ) == PG_TIMESTAMPTZ ? PG_TIMESTAMP : PG_TIMESTAMPTZ;

				if ( base::same_folded ( name, "date_trunc" ) && item->count > 1 ) {

					uint32_t inner = expression_type ( plan, item->list [ 1 ] );

					return inner == PG_TIMESTAMPTZ ? PG_TIMESTAMPTZ : PG_TIMESTAMP;

				}

				if ( base::same_folded ( name, "length" ) || base::same_folded ( name, "char_length" ) || base::same_folded ( name, "octet_length" ) || base::same_folded ( name, "strpos" ) || base::same_folded ( name, "position" ) || base::same_folded ( name, "pg_backend_pid" ) )
					return PG_INT4;

				if ( base::same_folded ( name, "avg" ) && item->count ) {

					uint32_t inner = expression_type ( plan, item->list [ 0 ] );

					return inner == PG_FLOAT8 || inner == PG_FLOAT4 ? PG_FLOAT8 : PG_NUMERIC;

				}

				if ( base::same_folded ( name, "round" ) && item->count ) {

					uint32_t inner = expression_type ( plan, item->list [ 0 ] );

					return inner == PG_FLOAT8 || inner == PG_FLOAT4 ? PG_FLOAT8 : PG_NUMERIC;

				}

				if ( base::same_folded ( name, "now" ) || base::same_folded ( name, "statement_timestamp" ) || base::same_folded ( name, "transaction_timestamp" ) || base::same_folded ( name, "clock_timestamp" ) )
					return PG_TIMESTAMPTZ;

				if ( base::same_folded ( name, "gen_random_uuid" ) || base::same_folded ( name, "uuid_generate_v4" ) )
					return PG_UUID;

				if ( base::same_folded ( name, "count" ) || base::same_folded ( name, "instr" ) || base::same_folded ( name, "row_number" ) || base::same_folded ( name, "rank" ) || base::same_folded ( name, "dense_rank" ) || base::same_folded ( name, "ntile" ) || base::same_folded ( name, "unixepoch" ) || base::same_folded ( name, "changes" ) || base::same_folded ( name, "last_insert_rowid" ) || base::same_folded ( name, "random" ) || base::same_folded ( name, "sign" ) )
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

	int32_t column_modifier ( const engine::prepared_select* plan, uint32_t column ) {

		if ( !plan || column >= plan->output_count )
			return -1;

		const sql::expression* item = plan->outputs [ column ];

		if ( !item || item->kind != sql::EXPRESSION_COLUMN || item->depth != 0 || item->source < 0 || static_cast < uint32_t > ( item->source ) >= plan->source_count || item->column < 0 )
			return -1;

		const engine::prepared_source& source = plan->sources [ item->source ];

		if ( !source.table || static_cast < uint32_t > ( item->column ) >= source.table->column_count )
			return -1;

		const engine::column_info& info = source.table->columns [ item->column ];
		uint32_t oid = declared_type ( info.type );

		if ( oid == PG_NUMERIC && info.precision > 0 )
			return ( info.precision << 16 | base::max ( info.scale, 0 ) ) + 4;

		if ( oid != PG_VARCHAR && oid != PG_BPCHAR )
			return -1;

		const char* open = info.type;

		while ( *open && *open != '(' )
			open++;

		int32_t length = 0;

		for ( const char* at = *open ? open + 1 : open; *at >= '0' && *at <= '9'; at++ )
			length = base::min ( length * 10 + ( *at - '0' ), 10485760 );

		return length ? length + 4 : -1;

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

		if ( item.kind == storage::VALUE_INTEGER || item.kind == storage::VALUE_REAL || item.kind == storage::VALUE_DECIMAL ) {

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
		uint32_t element = array_element ( type );

		if ( element && item.kind == storage::VALUE_TEXT ) {

			engine::array_item* items = nullptr;
			uint32_t count = 0;
			base::arena memory;

			if ( !engine::split_array ( memory, item.bytes, item.size, items, count ) )
				return false;

			bool nulls = false;

			for ( uint32_t i = 0; i < count; i++ )
				nulls = nulls || items [ i ].null;

			size_t mark = out.size;

			put_length ( out, 0 );
			put_big ( out, count ? 1 : 0, 4 );
			put_big ( out, nulls ? 1 : 0, 4 );
			put_big ( out, element, 4 );

			if ( count ) {

				put_big ( out, count, 4 );
				put_big ( out, 1, 4 );

			}

			for ( uint32_t i = 0; i < count; i++ ) {

				storage::value part = items [ i ].null ? storage::null_value ( ) : storage::text_value ( items [ i ].text, items [ i ].size );

				if ( !encode_value ( link, part, element, true ) )
					return false;

			}

			uint32_t length = static_cast < uint32_t > ( out.size - mark - 4 );

			out.data [ mark ] = static_cast < char > ( length >> 24 );
			out.data [ mark + 1 ] = static_cast < char > ( length >> 16 );
			out.data [ mark + 2 ] = static_cast < char > ( length >> 8 );
			out.data [ mark + 3 ] = static_cast < char > ( length );

			return true;

		}

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

			case PG_INTERVAL: {

				engine::interval_parts parts = { };

				if ( item.kind != storage::VALUE_TEXT || !engine::parse_interval ( item.bytes, item.size, parts ) )
					return false;

				put_length ( out, 16 );
				put_big ( out, static_cast < uint64_t > ( parts.micros ), 8 );
				put_big ( out, static_cast < uint64_t > ( parts.days ), 4 );
				put_big ( out, static_cast < uint64_t > ( parts.months ), 4 );

				return true;

			}

			case PG_DATE:
			case PG_TIMESTAMP:
			case PG_TIMESTAMPTZ: {

				int64_t days = 0;
				int64_t micros = 0;
				bool has_time = false;
				int32_t infinite = item.kind == storage::VALUE_TEXT ? engine::moment_infinity ( item.bytes, item.size ) : 0;

				if ( infinite ) {

					put_length ( out, type == PG_DATE ? 4 : 8 );

					if ( type == PG_DATE )
						put_big ( out, infinite > 0 ? 0x7FFFFFFFull : 0x80000000ull, 4 );
					else
						put_big ( out, infinite > 0 ? 0x7FFFFFFFFFFFFFFFull : 0x8000000000000000ull, 8 );

					return true;

				}

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

	static uint32_t array_element ( uint32_t type ) {

		switch ( type ) {

			case 1000: return PG_BOOL;
			case 1001: return PG_BYTEA;
			case 1005: return PG_INT2;
			case 1007: return PG_INT4;
			case 1016: return PG_INT8;
			case 1028: return PG_OID;
			case 1009: case 1015: case 1014: return PG_TEXT;
			case 1021: return PG_FLOAT4;
			case 1022: return PG_FLOAT8;
			case 1231: return PG_NUMERIC;
			case 2951: return PG_UUID;
			case 1115: return PG_TIMESTAMP;
			case 1185: return PG_TIMESTAMPTZ;
			case 1182: return PG_DATE;
			case 1183: return PG_TIME;
			case 199: return PG_JSON;
			case 3807: return PG_JSONB;
			default: return 0;

		}

	}

	static bool decode_array ( base::arena& memory, const uint8_t* data, int32_t size, uint32_t element, strata_value& out ) {

		if ( size < 12 )
			return false;

		uint32_t dimensions = static_cast < uint32_t > ( read_big ( data, 4 ) );
		uint32_t inner = static_cast < uint32_t > ( read_big ( data + 8, 4 ) );
		size_t at = 12;
		uint64_t count = dimensions ? 1 : 0;

		if ( dimensions > 6 || static_cast < size_t > ( size ) < at + dimensions * 8 )
			return false;

		for ( uint32_t d = 0; d < dimensions; d++ )
			count *= read_big ( data + at + d * 8, 4 );

		at += dimensions * 8;

		if ( inner )
			element = inner;

		base::text made;

		made.add ( '{' );

		for ( uint64_t i = 0; i < count; i++ ) {

			if ( at + 4 > static_cast < size_t > ( size ) )
				return false;

			int32_t length = static_cast < int32_t > ( read_big ( data + at, 4 ) );

			at += 4;

			if ( length > 0 && at + static_cast < size_t > ( length ) > static_cast < size_t > ( size ) )
				return false;

			if ( i )
				made.add ( ',' );

			strata_value item = { };

			if ( !decode_parameter ( memory, data + at, length, element, true, item ) )
				return false;

			switch ( item.type ) {

				case STRATA_NULL:
					made.add ( "NULL" );
					break;

				case STRATA_INTEGER:
					made.signed_number ( item.data.integer );
					break;

				case STRATA_REAL:
					made.real ( item.data.real );
					break;

				default: {

					const char* bytes = static_cast < const char* > ( item.data.bytes );

					made.add ( '"' );

					for ( uint32_t k = 0; k < item.size; k++ ) {

						if ( bytes [ k ] == '"' || bytes [ k ] == '\\' )
							made.add ( '\\' );

						made.add ( bytes [ k ] );

					}

					made.add ( '"' );

				}

			}

			if ( length > 0 )
				at += static_cast < size_t > ( length );

		}

		made.add ( '}' );

		return keep_text ( memory, made, out );

	}

	bool decode_parameter ( base::arena& memory, const uint8_t* data, int32_t size, uint32_t type, bool binary, strata_value& out ) {

		out = { };

		if ( size < 0 ) {

			out.type = STRATA_NULL;

			return true;

		}

		if ( binary && array_element ( type ) )
			return decode_array ( memory, data, size, array_element ( type ), out );

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

			case PG_INTERVAL: {

				if ( size != 16 )
					return false;

				engine::interval_parts parts = { };
				base::text made;

				parts.micros = static_cast < int64_t > ( read_big ( data, 8 ) );
				parts.days = static_cast < int32_t > ( read_big ( data + 8, 4 ) );
				parts.months = static_cast < int32_t > ( read_big ( data + 12, 4 ) );
				engine::write_interval ( parts, made );

				return keep_text ( memory, made, out );

			}

			case PG_DATE: {

				if ( size != 4 )
					return false;

				base::text made;
				int32_t days = static_cast < int32_t > ( read_big ( data, 4 ) );

				if ( days == 0x7FFFFFFF || days == static_cast < int32_t > ( 0x80000000u ) )
					made.add ( days > 0 ? "infinity" : "-infinity" );
				else
					add_civil ( made, days );

				return keep_text ( memory, made, out );

			}

			case PG_TIMESTAMP:
			case PG_TIMESTAMPTZ: {

				if ( size != 8 )
					return false;

				int64_t micros = static_cast < int64_t > ( read_big ( data, 8 ) );
				int64_t days = micros >= 0 ? micros / 86400000000ll : ( micros - 86399999999ll ) / 86400000000ll;
				base::text made;

				if ( micros == static_cast < int64_t > ( 0x7FFFFFFFFFFFFFFFull ) || micros == static_cast < int64_t > ( 0x8000000000000000ull ) ) {

					made.add ( micros > 0 ? "infinity" : "-infinity" );

					return keep_text ( memory, made, out );

				}

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

				if ( !keep_text ( memory, made, out ) )
					return false;

				out.type = STRATA_NUMERIC;

				return true;

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

		if ( base::starts_with ( message, "no such view" ) || base::starts_with ( message, "no such sequence" ) || ( base::starts_with ( message, "relation \"" ) && base::find_folded ( message, "does not exist" ) ) )
			return "42P01";

		if ( base::starts_with ( message, "no such index" ) || ( base::starts_with ( message, "constraint \"" ) && base::find_folded ( message, "does not exist" ) ) )
			return "42704";

		if ( base::starts_with ( message, "nextval: reached" ) )
			return "2200H";

		if ( base::starts_with ( message, "currval of sequence" ) )
			return "55000";

		if ( base::starts_with ( message, "setval: value" ) )
			return "22003";

		if ( base::find_folded ( message, "is not supported" ) )
			return "0A000";

		if ( base::starts_with ( message, "division by zero" ) )
			return "22012";

		if ( base::starts_with ( message, "invalid input syntax" ) )
			return "22P02";

		if ( base::starts_with ( message, "numeric field overflow" ) || base::starts_with ( message, "integer overflow" ) )
			return "22003";

		if ( base::starts_with ( message, "cannot drop schema" ) || base::starts_with ( message, "cannot drop constraint" ) )
			return "2BP01";

		if ( base::find_folded ( message, "busy" ) || base::find_folded ( message, "locked" ) )
			return "55P03";

		return "XX000";

	}

}
