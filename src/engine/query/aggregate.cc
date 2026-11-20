#include "engine/run/run.hh"

namespace engine {

	uint32_t hash_row ( const storage::value* values, uint32_t width ) {

		uint32_t hash = 0x6A09E667u;

		for ( uint32_t i = 0; i < width; i++ )
			hash = hash_value ( values [ i ], hash );

		return hash;

	}

	bool same_row ( const storage::value* a, const storage::value* b, uint32_t width ) {

		for ( uint32_t i = 0; i < width; i++ )
			if ( !same_value ( a [ i ], b [ i ] ) )
				return false;

		return true;

	}

	storage::value* copy_row ( base::arena& memory, const storage::value* values, uint32_t width ) {

		storage::value* made = memory.make < storage::value > ( width ? width : 1 );

		if ( !made )
			return nullptr;

		for ( uint32_t i = 0; i < width; i++ ) {

			made [ i ] = keep_value ( memory, values [ i ] );

			if ( made [ i ].kind == storage::VALUE_NULL && values [ i ].kind != storage::VALUE_NULL )
				return nullptr;

		}

		return made;

	}

	static bool grow_set ( base::arena& memory, value_set& set ) {

		uint32_t grown = set.bucket_count ? set.bucket_count * 4 : 64;
		value_set_entry** buckets = memory.make < value_set_entry* > ( grown );

		if ( !buckets )
			return false;

		for ( uint32_t i = 0; i < set.bucket_count; i++ ) {

			value_set_entry* entry = set.buckets [ i ];

			while ( entry ) {

				value_set_entry* next = entry->next;
				uint32_t slot = entry->hash & ( grown - 1 );

				entry->next = buckets [ slot ];
				buckets [ slot ] = entry;
				entry = next;

			}

		}

		set.buckets = buckets;
		set.bucket_count = grown;

		return true;

	}

	bool set_has ( const value_set& set, const storage::value* values, uint32_t width ) {

		if ( !set.bucket_count )
			return false;

		uint32_t hash = hash_row ( values, width );

		for ( value_set_entry* entry = set.buckets [ hash & ( set.bucket_count - 1 ) ]; entry; entry = entry->next )
			if ( entry->hash == hash && same_row ( entry->values, values, width ) )
				return true;

		return false;

	}

	bool set_add ( base::arena& memory, value_set& set, const storage::value* values, uint32_t width, bool& added ) {

		added = false;

		if ( !set.bucket_count && !grow_set ( memory, set ) )
			return false;

		set.width = width;

		uint32_t hash = hash_row ( values, width );

		for ( value_set_entry* entry = set.buckets [ hash & ( set.bucket_count - 1 ) ]; entry; entry = entry->next )
			if ( entry->hash == hash && same_row ( entry->values, values, width ) )
				return true;

		value_set_entry* made = memory.make < value_set_entry > ( 1 );

		if ( !made )
			return false;

		made->hash = hash;
		made->values = copy_row ( memory, values, width );

		if ( !made->values )
			return false;

		for ( uint32_t i = 0; i < width; i++ )
			if ( values [ i ].kind == storage::VALUE_NULL )
				set.has_null = true;

		uint32_t slot = hash & ( set.bucket_count - 1 );

		made->next = set.buckets [ slot ];
		set.buckets [ slot ] = made;
		set.count++;
		added = true;

		if ( set.count > set.bucket_count * 2 && !grow_set ( memory, set ) )
			return false;

		return true;

	}

	void start_aggregate ( aggregate_state& state, const sql::expression* item ) {

		state = { };
		state.distinct = item->distinct;

		switch ( static_cast < function_kind > ( item->column ) ) {

			case FUNCTION_COUNT: state.kind = item->star || !item->count ? AGGREGATE_COUNT_ALL : AGGREGATE_COUNT; break;
			case FUNCTION_SUM: state.kind = AGGREGATE_SUM; break;
			case FUNCTION_TOTAL: state.kind = AGGREGATE_TOTAL; break;
			case FUNCTION_AVG: state.kind = AGGREGATE_AVG; break;
			case FUNCTION_MIN_AGGREGATE: state.kind = AGGREGATE_MIN; break;
			case FUNCTION_MAX_AGGREGATE: state.kind = AGGREGATE_MAX; break;
			case FUNCTION_JSON_GROUP_ARRAY: state.kind = AGGREGATE_JSON_ARRAY; break;
			case FUNCTION_JSON_GROUP_OBJECT: state.kind = AGGREGATE_JSON_OBJECT; break;
			default: state.kind = AGGREGATE_GROUP_CONCAT; break;

		}

	}

	static bool append_joined ( run_context& ctx, aggregate_state& state, const uint8_t* bytes, size_t size ) {

		if ( state.joined_size + size + 1 > state.joined_room ) {

			size_t wanted = base::max < size_t > ( state.joined_room * 2, state.joined_size + size + 64 );

			if ( wanted > 0x7FFFFFFF )
				return fail_run ( ctx, "group_concat() grew larger than 2 GB" );

			char* grown = ctx.memory->make < char > ( wanted );

			if ( !grown )
				return fail_run ( ctx, "out of memory" );

			if ( state.joined_size )
				base::copy ( grown, state.joined, state.joined_size );

			state.joined = grown;
			state.joined_room = static_cast < uint32_t > ( wanted );

		}

		if ( size )
			base::copy ( state.joined + state.joined_size, bytes, size );

		state.joined_size += static_cast < uint32_t > ( size );

		return true;

	}

	bool step_aggregate ( run_context& ctx, aggregate_state& state, const storage::value* args, uint32_t count ) {

		state.improved = false;

		if ( state.kind == AGGREGATE_COUNT_ALL ) {

			state.count++;

			return true;

		}

		const storage::value& item = args [ 0 ];

		if ( state.kind == AGGREGATE_JSON_ARRAY || state.kind == AGGREGATE_JSON_OBJECT ) {

			base::text piece;

			if ( state.kind == AGGREGATE_JSON_OBJECT ) {

				if ( item.kind == storage::VALUE_NULL )
					return true;

				storage::value key = text_of ( *ctx.scratch, item );

				key.subtype = 0;

				if ( !append_json ( ctx, piece, key ) )
					return false;

				piece.add ( ':' );

				if ( !append_json ( ctx, piece, args [ 1 ] ) )
					return false;

			} else if ( !append_json ( ctx, piece, item ) )
				return false;

			if ( state.count && !append_joined ( ctx, state, reinterpret_cast < const uint8_t* > ( "," ), 1 ) )
				return false;

			state.count++;

			return append_joined ( ctx, state, reinterpret_cast < const uint8_t* > ( piece.value ( ) ), piece.size );

		}

		if ( item.kind == storage::VALUE_NULL )
			return true;

		if ( state.distinct ) {

			bool added = false;

			if ( !set_add ( *ctx.memory, state.seen, &item, 1, added ) )
				return fail_run ( ctx, "out of memory" );

			if ( !added )
				return true;

		}

		switch ( state.kind ) {

			case AGGREGATE_COUNT:
				state.count++;

				return true;

			case AGGREGATE_SUM:
			case AGGREGATE_TOTAL:
			case AGGREGATE_AVG: {

				storage::value number = numeric_of ( item );

				state.count++;

				if ( number.kind == storage::VALUE_INTEGER || number.kind == storage::VALUE_DECIMAL ) {

					decimal_number exact = { };

					if ( !state.decimal_broken && ( !decimal_of ( number, exact ) || !decimal_add ( state.sum_decimal, exact, false, state.sum_decimal ) ) )
						state.decimal_broken = true;

				}

				if ( number.kind == storage::VALUE_DECIMAL ) {

					decimal_number exact = { };

					decimal_of ( number, exact );
					state.decimal_seen = true;
					state.sum_real += decimal_real ( exact );

				} else if ( number.kind == storage::VALUE_INTEGER ) {

					state.sum_real += static_cast < double > ( number.integer );

					if ( !state.overflow && __builtin_add_overflow ( state.sum_integer, number.integer, &state.sum_integer ) )
						state.overflow = true;

				} else {

					state.sum_real += number.real;
					state.real_seen = true;

				}

				return true;

			}

			case AGGREGATE_MIN:
			case AGGREGATE_MAX: {

				int32_t order = state.has_best ? order_values ( item, state.best ) : 0;

				if ( !state.has_best || ( state.kind == AGGREGATE_MIN ? order < 0 : order > 0 ) ) {

					state.best = keep_value ( *ctx.memory, item );
					state.has_best = true;
					state.improved = true;

				}

				state.count++;

				return true;

			}

			default: {

				storage::value text = text_of ( *ctx.scratch, item );

				if ( state.count ) {

					if ( count > 1 ) {

						if ( args [ 1 ].kind != storage::VALUE_NULL ) {

							storage::value separator = text_of ( *ctx.scratch, args [ 1 ] );

							if ( !append_joined ( ctx, state, separator.bytes, separator.size ) )
								return false;

						}

					} else if ( !append_joined ( ctx, state, reinterpret_cast < const uint8_t* > ( "," ), 1 ) )
						return false;

				}

				state.count++;

				return append_joined ( ctx, state, text.bytes, text.size );

			}

		}

	}

	bool finish_aggregate ( run_context& ctx, aggregate_state& state, storage::value& out ) {

		switch ( state.kind ) {

			case AGGREGATE_COUNT:
			case AGGREGATE_COUNT_ALL:
				out = storage::integer_value ( static_cast < int64_t > ( state.count ) );

				return true;

			case AGGREGATE_SUM:
				if ( !state.count )
					out = storage::null_value ( );
				else if ( state.real_seen )
					out = storage::real_value ( state.sum_real );
				else if ( state.decimal_seen && !state.decimal_broken )
					out = decimal_result ( state.sum_decimal );
				else if ( state.overflow && !state.decimal_broken && ctx.schema->postgres )
					out = decimal_result ( state.sum_decimal );
				else if ( state.overflow )
					return fail_run ( ctx, "integer overflow in sum(), use total() for a real sum" );
				else
					out = storage::integer_value ( state.sum_integer );

				return true;

			case AGGREGATE_TOTAL:
				out = storage::real_value ( state.sum_real );

				return true;

			case AGGREGATE_AVG: {

				if ( !state.count ) {

					out = storage::null_value ( );

					return true;

				}

				decimal_number count = { static_cast < __int128 > ( state.count ), 0 };
				decimal_number average = { };

				if ( !state.real_seen && !state.decimal_broken && ( state.decimal_seen || ctx.schema->postgres ) && decimal_divide ( state.sum_decimal, count, average ) )
					out = decimal_result ( average );
				else
					out = storage::real_value ( state.sum_real / static_cast < double > ( state.count ) );

				return true;

			}

			case AGGREGATE_MIN:
			case AGGREGATE_MAX:
				out = state.has_best ? state.best : storage::null_value ( );

				return true;

			case AGGREGATE_JSON_ARRAY:
			case AGGREGATE_JSON_OBJECT: {

				size_t size = state.joined_size + 2;
				char* made = ctx.scratch->make < char > ( size + 1 );

				if ( !made )
					return fail_run ( ctx, "out of memory" );

				made [ 0 ] = state.kind == AGGREGATE_JSON_ARRAY ? '[' : '{';

				if ( state.joined_size )
					base::copy ( made + 1, state.joined, state.joined_size );

				made [ size - 1 ] = state.kind == AGGREGATE_JSON_ARRAY ? ']' : '}';
				out = storage::text_value ( made, size );
				out.subtype = 1;

				return true;

			}

			default:
				out = state.count ? storage::text_value ( state.joined ? state.joined : "", state.joined_size ) : storage::null_value ( );

				return true;

		}

	}

}
