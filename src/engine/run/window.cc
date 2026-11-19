#include "engine/run/run.hh"
#include "base/sort/sort.hh"

namespace engine {

	bool capture_window_row ( run_context& ctx, query_state& scope, window_rows& out ) {

		prepared_select* plan = scope.plan;
		uint32_t sources = plan->source_count ? plan->source_count : 1;
		window_row* row = ctx.memory->make < window_row > ( 1 );

		if ( !row )
			return fail_run ( ctx, "out of memory" );

		row->rowids = ctx.memory->make < int64_t > ( sources );
		row->rows = ctx.memory->make < storage::value* > ( sources );
		row->null_rows = ctx.memory->make < bool > ( sources );

		if ( !row->rowids || !row->rows || !row->null_rows )
			return fail_run ( ctx, "out of memory" );

		for ( uint32_t s = 0; s < plan->source_count; s++ ) {

			source_state& source = scope.sources [ s ];

			row->null_rows [ s ] = source.null_row || !source.values;
			row->rowids [ s ] = source.rowid;
			row->rows [ s ] = source.plan->table ? nullptr : source.values;

		}

		if ( scope.aggregate_values ) {

			row->aggregates = copy_row ( *ctx.memory, scope.aggregate_values, plan->aggregate_count );

			if ( !row->aggregates )
				return fail_run ( ctx, "out of memory" );

		}

		storage::value* inputs = ctx.scratch->make < storage::value > ( plan->window_width + 1 );

		if ( !inputs )
			return fail_run ( ctx, "out of memory" );

		for ( uint32_t w = 0; w < plan->window_count; w++ ) {

			const sql::expression* call = plan->windows [ w ];
			const sql::window_spec& spec = *call->window;
			storage::value* at = inputs + plan->window_offsets [ w ];

			for ( uint32_t i = 0; i < call->count; i++ )
				if ( !evaluate ( ctx, &scope, call->list [ i ], *at++ ) )
					return false;

			for ( uint32_t i = 0; i < spec.partition_count; i++ )
				if ( !evaluate ( ctx, &scope, spec.partition [ i ], *at++ ) )
					return false;

			for ( uint32_t i = 0; i < spec.order_count; i++ )
				if ( !evaluate ( ctx, &scope, spec.order [ i ].value, *at++ ) )
					return false;

		}

		row->inputs = copy_row ( *ctx.memory, inputs, plan->window_width );
		row->index = out.count;

		if ( !row->inputs || !base::push ( *ctx.memory, out.rows, out.count, out.room, row ) )
			return fail_run ( ctx, "out of memory" );

		return true;

	}

	bool restore_window_row ( run_context& ctx, query_state& scope, const window_row& row ) {

		prepared_select* plan = scope.plan;

		for ( uint32_t s = 0; s < plan->source_count; s++ ) {

			source_state& source = scope.sources [ s ];

			source.null_row = row.null_rows [ s ];
			source.values = nullptr;

			if ( source.null_row )
				continue;

			source.rowid = row.rowids [ s ];

			if ( !source.plan->table ) {

				source.values = row.rows [ s ];

				continue;

			}

			bool found = false;

			if ( !read_row ( ctx, *source.plan->table, source.rowid, source.values, found ) )
				return false;

			if ( !found )
				source.null_row = true;

		}

		scope.aggregate_values = row.aggregates;

		return true;

	}

	static int32_t compare_window ( void* context, const void* a, const void* b ) {

		const window_order& order = *static_cast < const window_order* > ( context );
		const window_row& x = *static_cast < const window_row* > ( a );
		const window_row& y = *static_cast < const window_row* > ( b );

		for ( uint32_t k = 0; k < order.partitions + order.orders; k++ ) {

			int32_t result = order_values ( x.inputs [ order.offset + k ], y.inputs [ order.offset + k ] );

			if ( result )
				return k >= order.partitions && order.spec->order [ k - order.partitions ].descending ? -result : result;

		}

		return 0;

	}

	static bool same_keys ( const window_row& a, const window_row& b, uint32_t offset, uint32_t count ) {

		for ( uint32_t k = 0; k < count; k++ )
			if ( order_values ( a.inputs [ offset + k ], b.inputs [ offset + k ] ) )
				return false;

		return true;

	}

	static bool frame_offset ( run_context& ctx, query_state& scope, const sql::expression* item, int64_t& out ) {

		out = 0;

		if ( !item )
			return true;

		storage::value value;

		if ( !evaluate ( ctx, &scope, item, value ) )
			return false;

		storage::value number = apply_affinity ( *ctx.scratch, value, sql::AFFINITY_INTEGER );

		if ( number.kind != storage::VALUE_INTEGER || number.integer < 0 )
			return fail_run ( ctx, "a window frame offset must be a whole number of zero or more" );

		out = number.integer;

		return true;

	}

	static int64_t bound_position ( sql::bound_kind kind, int64_t offset, int64_t k, int64_t first, int64_t last ) {

		switch ( kind ) {

			case sql::BOUND_UNBOUNDED_PRECEDING: return first;
			case sql::BOUND_PRECEDING: return k - offset;
			case sql::BOUND_FOLLOWING: return k + offset;
			case sql::BOUND_UNBOUNDED_FOLLOWING: return last;
			default: return k;

		}

	}

	bool compute_windows ( run_context& ctx, query_state& scope, window_rows& rows, storage::value*& out ) {

		prepared_select* plan = scope.plan;
		uint32_t n = rows.count;
		uint32_t width = plan->window_count;

		out = ctx.memory->make < storage::value > ( static_cast < size_t > ( n ) * width + 1 );

		window_row** sorted = ctx.memory->make < window_row* > ( n + 1 );
		uint32_t* peer_first = ctx.memory->make < uint32_t > ( n + 1 );
		uint32_t* peer_last = ctx.memory->make < uint32_t > ( n + 1 );
		uint32_t* dense = ctx.memory->make < uint32_t > ( n + 1 );

		if ( !out || !sorted || !peer_first || !peer_last || !dense )
			return fail_run ( ctx, "out of memory" );

		for ( uint32_t w = 0; w < width; w++ ) {

			const sql::expression* call = plan->windows [ w ];
			const sql::window_spec& spec = *call->window;
			uint32_t base_offset = plan->window_offsets [ w ];
			uint32_t key_offset = base_offset + call->count;
			window_order order = { key_offset, spec.partition_count, spec.order_count, &spec };
			int64_t start_offset = 0;
			int64_t end_offset = 0;
			function_kind kind = static_cast < function_kind > ( call->column );
			bool aggregate = kind <= FUNCTION_JSON_GROUP_OBJECT && kind >= FUNCTION_COUNT;

			if ( !frame_offset ( ctx, scope, spec.start_offset, start_offset ) || !frame_offset ( ctx, scope, spec.end_offset, end_offset ) )
				return false;

			if ( spec.frame == sql::FRAME_RANGE && ( spec.start == sql::BOUND_PRECEDING || spec.start == sql::BOUND_FOLLOWING || spec.end == sql::BOUND_PRECEDING || spec.end == sql::BOUND_FOLLOWING ) )
				return fail_run ( ctx, "RANGE frames with an offset are not supported, use ROWS" );

			base::copy ( sorted, rows.rows, sizeof ( window_row* ) * n );

			if ( !base::sort_pointers ( reinterpret_cast < void** > ( sorted ), n, compare_window, &order ) )
				return fail_run ( ctx, "out of memory while sorting a window" );

			uint32_t ps = 0;

			while ( ps < n ) {

				uint32_t pe = ps + 1;

				while ( pe < n && same_keys ( *sorted [ ps ], *sorted [ pe ], key_offset, spec.partition_count ) )
					pe++;

				uint32_t groups = 0;

				for ( uint32_t k = ps; k < pe; ) {

					uint32_t end = k + 1;

					while ( end < pe && same_keys ( *sorted [ k ], *sorted [ end ], key_offset + spec.partition_count, spec.order_count ) )
						end++;

					groups++;

					for ( uint32_t j = k; j < end; j++ ) {

						peer_first [ j ] = k;
						peer_last [ j ] = end - 1;
						dense [ j ] = groups;

					}

					k = end;

				}

				aggregate_state state;
				int64_t stepped = static_cast < int64_t > ( ps ) - 1;
				bool running = spec.frame == sql::FRAME_DEFAULT || spec.start == sql::BOUND_UNBOUNDED_PRECEDING;
				int64_t count = pe - ps;

				if ( spec.frame != sql::FRAME_DEFAULT && spec.end == sql::BOUND_PRECEDING )
					running = false;

				for ( uint32_t k = ps; k < pe; k++ ) {

					const window_row& row = *sorted [ k ];
					const storage::value* args = row.inputs + base_offset;
					storage::value value = storage::null_value ( );
					int64_t fs = ps;
					int64_t fe = pe - 1;

					if ( spec.frame == sql::FRAME_DEFAULT ) {

						if ( spec.order_count )
							fe = peer_last [ k ];

					} else {

						bool by_rows = spec.frame == sql::FRAME_ROWS;

						fs = bound_position ( spec.start, start_offset, k, ps, pe - 1 );
						fe = bound_position ( spec.end, end_offset, k, ps, pe - 1 );

						if ( !by_rows && spec.start == sql::BOUND_CURRENT )
							fs = peer_first [ k ];

						if ( !by_rows && spec.end == sql::BOUND_CURRENT )
							fe = peer_last [ k ];

					}

					if ( fs < ps )
						fs = ps;

					if ( fe > static_cast < int64_t > ( pe ) - 1 )
						fe = pe - 1;

					switch ( kind ) {

						case FUNCTION_ROW_NUMBER:
							value = storage::integer_value ( k - ps + 1 );
							break;

						case FUNCTION_RANK:
							value = storage::integer_value ( peer_first [ k ] - ps + 1 );
							break;

						case FUNCTION_DENSE_RANK:
							value = storage::integer_value ( dense [ k ] );
							break;

						case FUNCTION_PERCENT_RANK:
							value = storage::real_value ( count > 1 ? static_cast < double > ( peer_first [ k ] - ps ) / static_cast < double > ( count - 1 ) : 0.0 );
							break;

						case FUNCTION_CUME_DIST:
							value = storage::real_value ( static_cast < double > ( peer_last [ k ] - ps + 1 ) / static_cast < double > ( count ) );
							break;

						case FUNCTION_NTILE: {

							int64_t buckets = 0;

							if ( !integer_of ( args [ 0 ], buckets ) || buckets <= 0 )
								return fail_run ( ctx, "ntile() needs a positive number of buckets" );

							int64_t position = k - ps;
							int64_t size = count / buckets;
							int64_t extra = count % buckets;
							int64_t big = extra * ( size + 1 );

							value = storage::integer_value ( size == 0 ? position + 1 : position < big ? position / ( size + 1 ) + 1 : extra + ( position - big ) / size + 1 );
							break;

						}

						case FUNCTION_LAG:
						case FUNCTION_LEAD: {

							int64_t step = 1;

							if ( call->count > 1 && !integer_of ( args [ 1 ], step ) )
								step = 1;

							int64_t target = kind == FUNCTION_LAG ? static_cast < int64_t > ( k ) - step : static_cast < int64_t > ( k ) + step;

							if ( target >= ps && target < pe )
								value = sorted [ target ]->inputs [ base_offset ];
							else if ( call->count > 2 )
								value = args [ 2 ];

							break;

						}

						case FUNCTION_FIRST_VALUE:
							if ( fs <= fe )
								value = sorted [ fs ]->inputs [ base_offset ];

							break;

						case FUNCTION_LAST_VALUE:
							if ( fs <= fe )
								value = sorted [ fe ]->inputs [ base_offset ];

							break;

						case FUNCTION_NTH_VALUE: {

							int64_t place = 0;

							if ( !integer_of ( args [ 1 ], place ) || place <= 0 )
								return fail_run ( ctx, "nth_value() needs a position of 1 or more" );

							if ( fs + place - 1 <= fe )
								value = sorted [ fs + place - 1 ]->inputs [ base_offset ];

							break;

						}

						default: {

							if ( !aggregate )
								break;

							if ( running ) {

								if ( k == ps )
									start_aggregate ( state, call );

								while ( stepped < fe ) {

									stepped++;

									if ( !step_aggregate ( ctx, state, sorted [ stepped ]->inputs + base_offset, call->count ) )
										return false;

								}

							} else {

								start_aggregate ( state, call );

								for ( int64_t j = fs; j <= fe; j++ )
									if ( !step_aggregate ( ctx, state, sorted [ j ]->inputs + base_offset, call->count ) )
										return false;

							}

							if ( !finish_aggregate ( ctx, state, value ) )
								return false;

							break;

						}

					}

					out [ static_cast < size_t > ( row.index ) * width + w ] = keep_value ( *ctx.memory, value );

				}

				ps = pe;

			}

		}

		return true;

	}

}
