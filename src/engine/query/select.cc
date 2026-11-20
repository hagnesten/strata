#include "engine/run/run.hh"
#include "base/sort/sort.hh"

namespace engine {

	static int32_t compare_sorted ( void* context, const void* a, const void* b ) {

		const output_pipe& pipe = *static_cast < const output_pipe* > ( context );
		const sorted_row& x = *static_cast < const sorted_row* > ( a );
		const sorted_row& y = *static_cast < const sorted_row* > ( b );

		for ( uint32_t k = 0; k < pipe.key_count; k++ ) {

			int32_t order = pipe.nocase && pipe.nocase [ k ] ? order_folded ( x.keys [ k ], y.keys [ k ] ) : order_values ( x.keys [ k ], y.keys [ k ] );

			if ( order )
				return pipe.descending [ k ] ? -order : order;

		}

		return x.sequence == y.sequence ? 0 : x.sequence < y.sequence ? -1 : 1;

	}

	static void heap_up ( output_pipe& pipe, uint32_t at ) {

		while ( at ) {

			uint32_t parent = ( at - 1 ) / 2;

			if ( compare_sorted ( &pipe, pipe.rows [ at ], pipe.rows [ parent ] ) <= 0 )
				return;

			sorted_row* held = pipe.rows [ at ];

			pipe.rows [ at ] = pipe.rows [ parent ];
			pipe.rows [ parent ] = held;
			at = parent;

		}

	}

	static void heap_down ( output_pipe& pipe, uint32_t at ) {

		for ( ;; ) {

			uint32_t worst = at;
			uint32_t left = at * 2 + 1;
			uint32_t right = left + 1;

			if ( left < pipe.row_count && compare_sorted ( &pipe, pipe.rows [ left ], pipe.rows [ worst ] ) > 0 )
				worst = left;

			if ( right < pipe.row_count && compare_sorted ( &pipe, pipe.rows [ right ], pipe.rows [ worst ] ) > 0 )
				worst = right;

			if ( worst == at )
				return;

			sorted_row* held = pipe.rows [ at ];

			pipe.rows [ at ] = pipe.rows [ worst ];
			pipe.rows [ worst ] = held;
			at = worst;

		}

	}

	static bool keep_sorted ( run_context& ctx, output_pipe& pipe, const storage::value* values, const storage::value* keys ) {

		uint64_t sequence = pipe.arrivals++;

		if ( !pipe.bound )
			return true;

		bool full = pipe.bound > 0 && pipe.row_count >= static_cast < uint64_t > ( pipe.bound );

		if ( full ) {

			sorted_row probe = { const_cast < storage::value* > ( values ), const_cast < storage::value* > ( keys ), sequence };

			if ( compare_sorted ( &pipe, &probe, pipe.rows [ 0 ] ) >= 0 )
				return true;

		}

		sorted_row* row = ctx.memory->make < sorted_row > ( 1 );

		if ( !row )
			return fail_run ( ctx, "out of memory" );

		row->values = copy_row ( *ctx.memory, values, pipe.width );
		row->keys = copy_row ( *ctx.memory, keys, pipe.key_count );
		row->sequence = sequence;

		if ( !row->values || !row->keys )
			return fail_run ( ctx, "out of memory" );

		if ( full ) {

			pipe.rows [ 0 ] = row;
			heap_down ( pipe, 0 );

			return true;

		}

		if ( !base::push ( *ctx.memory, pipe.rows, pipe.row_count, pipe.row_room, row ) )
			return fail_run ( ctx, "out of memory" );

		if ( pipe.bound > 0 )
			heap_up ( pipe, pipe.row_count - 1 );

		return true;

	}

	static bool emit_row ( run_context& ctx, output_pipe& pipe, const storage::value* values, const storage::value* keys ) {

		if ( pipe.distinct ) {

			bool added = false;
			storage::value* folded = nullptr;

			for ( uint32_t c = 0; pipe.outputs && c < pipe.width; c++ ) {

				if ( !pipe.outputs [ c ] || !pipe.outputs [ c ]->nocase || values [ c ].kind != storage::VALUE_TEXT )
					continue;

				if ( !folded ) {

					folded = ctx.scratch->make < storage::value > ( pipe.width );

					if ( !folded )
						return fail_run ( ctx, "out of memory" );

					for ( uint32_t i = 0; i < pipe.width; i++ )
						folded [ i ] = values [ i ];

				}

				folded [ c ] = folded_value ( *ctx.scratch, values [ c ] );

			}

			if ( !set_add ( *ctx.memory, pipe.unique, folded ? folded : values, pipe.width, added ) )
				return fail_run ( ctx, "out of memory" );

			if ( !added )
				return true;

		}

		if ( pipe.sorting )
			return keep_sorted ( ctx, pipe, values, keys );

		if ( pipe.skip > 0 ) {

			pipe.skip--;

			return true;

		}

		if ( !pipe.remaining ) {

			pipe.stopped = true;

			return true;

		}

		if ( pipe.remaining > 0 )
			pipe.remaining--;

		if ( !pipe.sink->emit ( pipe.sink->context, values, pipe.width ) ) {

			pipe.stopped = true;

			return !ctx.problem->size;

		}

		if ( !pipe.remaining )
			pipe.stopped = true;

		return true;

	}

	static bool finish_pipe ( run_context& ctx, output_pipe& pipe ) {

		if ( !pipe.sorting )
			return true;

		if ( !base::sort_pointers ( reinterpret_cast < void** > ( pipe.rows ), pipe.row_count, compare_sorted, &pipe ) )
			return fail_run ( ctx, "out of memory while sorting" );

		pipe.sorting = false;
		pipe.distinct = false;

		for ( uint32_t i = 0; i < pipe.row_count && !pipe.stopped; i++ )
			if ( !emit_row ( ctx, pipe, pipe.rows [ i ]->values, nullptr ) )
				return false;

		return true;

	}

	static bool reject_early ( run_context& ctx, query_state& scope, output_pipe& pipe, bool& rejected ) {

		prepared_select* plan = scope.plan;

		rejected = false;

		if ( !pipe.sorting || pipe.bound <= 0 || pipe.distinct || !pipe.key_count || pipe.row_count < static_cast < uint64_t > ( pipe.bound ) )
			return true;

		storage::value* keys = static_cast < storage::value* > ( ctx.scratch->take_raw ( sizeof ( storage::value ) * pipe.key_count ) );

		if ( !keys )
			return fail_run ( ctx, "out of memory" );

		for ( uint32_t k = 0; k < pipe.key_count; k++ ) {

			const sql::expression* source = plan->order_output && plan->order_output [ k ] >= 0 ? plan->outputs [ plan->order_output [ k ] ] : plan->order_values [ k ];

			if ( !evaluate ( ctx, &scope, source, keys [ k ] ) )
				return false;

		}

		sorted_row probe = { nullptr, keys, pipe.arrivals };

		if ( compare_sorted ( &pipe, &probe, pipe.rows [ 0 ] ) >= 0 ) {

			pipe.arrivals++;
			rejected = true;

		}

		return true;

	}

	bool sort_rejects ( output_pipe& pipe, const storage::value& key ) {

		if ( !pipe.row_count || pipe.row_count < static_cast < uint64_t > ( pipe.bound ) )
			return false;

		storage::value keys [ 1 ] = { key };
		sorted_row probe = { nullptr, keys, pipe.arrivals };

		if ( compare_sorted ( &pipe, &probe, pipe.rows [ 0 ] ) < 0 )
			return false;

		pipe.arrivals++;

		return true;

	}

	static bool output_row ( run_context& ctx, query_state& scope, output_pipe& pipe, const storage::value* given ) {

		prepared_select* plan = scope.plan;
		storage::value* values = const_cast < storage::value* > ( given );

		if ( !values ) {

			bool rejected = false;

			if ( !reject_early ( ctx, scope, pipe, rejected ) )
				return false;

			if ( rejected )
				return true;

			values = static_cast < storage::value* > ( ctx.scratch->take_raw ( sizeof ( storage::value ) * ( plan->output_count + 1 ) ) );

			if ( !values )
				return fail_run ( ctx, "out of memory" );

			values [ plan->output_count ] = storage::null_value ( );

			for ( uint32_t i = 0; i < plan->output_count; i++ )
				if ( !evaluate ( ctx, &scope, plan->outputs [ i ], values [ i ] ) )
					return false;

		}

		storage::value* keys = nullptr;

		if ( pipe.key_count ) {

			keys = static_cast < storage::value* > ( ctx.scratch->take_raw ( sizeof ( storage::value ) * pipe.key_count ) );

			if ( !keys )
				return fail_run ( ctx, "out of memory" );

			for ( uint32_t k = 0; k < pipe.key_count; k++ ) {

				if ( plan->order_output && plan->order_output [ k ] >= 0 )
					keys [ k ] = values [ plan->order_output [ k ] ];
				else if ( !evaluate ( ctx, &scope, plan->order_values [ k ], keys [ k ] ) )
					return false;

			}

		}

		return emit_row ( ctx, pipe, values, keys );

	}

	static bool take_snapshot ( query_state& scope, group_state& group ) {

		prepared_select* plan = scope.plan;

		for ( uint32_t s = 0; s < plan->source_count; s++ ) {

			source_state& source = scope.sources [ s ];

			group.null_rows [ s ] = source.null_row || !source.values;
			group.rowids [ s ] = source.rowid;
			group.rows [ s ] = source.plan->table ? nullptr : source.values;

		}

		group.has_snapshot = true;

		return true;

	}

	static bool restore_snapshot ( run_context& ctx, query_state& scope, group_state& group ) {

		prepared_select* plan = scope.plan;

		for ( uint32_t s = 0; s < plan->source_count; s++ ) {

			source_state& source = scope.sources [ s ];

			source.null_row = !group.has_snapshot || group.null_rows [ s ];
			source.values = nullptr;

			if ( source.null_row )
				continue;

			source.rowid = group.rowids [ s ];

			if ( !source.plan->table ) {

				source.values = group.rows [ s ];

				continue;

			}

			bool found = false;

			if ( !read_row ( ctx, *source.plan->table, source.rowid, source.values, found ) )
				return false;

			if ( !found )
				source.null_row = true;

		}

		return true;

	}

	static bool grow_groups ( run_context& ctx, group_table& table ) {

		uint32_t grown = table.bucket_count ? table.bucket_count * 4 : 64;
		group_state** buckets = ctx.memory->make < group_state* > ( grown );

		if ( !buckets )
			return fail_run ( ctx, "out of memory" );

		for ( group_state* group = table.first; group; group = group->next ) {

			uint32_t slot = group->hash & ( grown - 1 );

			group->chain = buckets [ slot ];
			buckets [ slot ] = group;

		}

		table.buckets = buckets;
		table.bucket_count = grown;

		return true;

	}

	group_state* new_group ( run_context& ctx, query_state& scope, group_table& table, const storage::value* keys, uint32_t hash ) {

		prepared_select* plan = scope.plan;
		group_state* group = ctx.memory->make < group_state > ( 1 );
		uint32_t sources = plan->source_count ? plan->source_count : 1;

		if ( !group ) {

			fail_run ( ctx, "out of memory" );

			return nullptr;

		}

		group->hash = hash;
		group->keys = copy_row ( *ctx.memory, keys, plan->group_count );
		group->states = ctx.memory->make < aggregate_state > ( plan->aggregate_count ? plan->aggregate_count : 1 );
		group->rowids = ctx.memory->make < int64_t > ( sources );
		group->rows = ctx.memory->make < storage::value* > ( sources );
		group->null_rows = ctx.memory->make < bool > ( sources );

		if ( !group->keys || !group->states || !group->rowids || !group->rows || !group->null_rows ) {

			fail_run ( ctx, "out of memory" );

			return nullptr;

		}

		for ( uint32_t a = 0; a < plan->aggregate_count; a++ )
			start_aggregate ( group->states [ a ], plan->aggregates [ a ] );

		if ( table.last )
			table.last->next = group;
		else
			table.first = group;

		table.last = group;
		table.count++;

		if ( !table.bucket_count || table.count > table.bucket_count * 2 ) {

			if ( !grow_groups ( ctx, table ) )
				return nullptr;

		} else {

			uint32_t slot = hash & ( table.bucket_count - 1 );

			group->chain = table.buckets [ slot ];
			table.buckets [ slot ] = group;

		}

		return group;

	}

	bool step_group ( run_context& ctx, query_state& scope, group_state& group ) {

		prepared_select* plan = scope.plan;

		for ( uint32_t a = 0; a < plan->aggregate_count; a++ ) {

			const sql::expression* call = plan->aggregates [ a ];
			storage::value local [ 4 ];
			storage::value* args = call->count <= 4 ? local : static_cast < storage::value* > ( ctx.scratch->take_raw ( sizeof ( storage::value ) * call->count ) );

			if ( call->count && !args )
				return fail_run ( ctx, "out of memory" );

			for ( uint32_t i = 0; i < call->count; i++ )
				if ( !evaluate ( ctx, &scope, call->list [ i ], args [ i ] ) )
					return false;

			if ( !step_aggregate ( ctx, group.states [ a ], args, call->count ) )
				return false;

			if ( static_cast < int32_t > ( a ) == plan->snapshot_aggregate && group.states [ a ].improved )
				take_snapshot ( scope, group );

		}

		return true;

	}

	static bool group_step ( run_context& ctx, query_state& scope, group_table& table ) {

		prepared_select* plan = scope.plan;

		if ( !plan->group_count && table.count == 1 && table.first )
			return step_group ( ctx, scope, *table.first );

		storage::value* keys = static_cast < storage::value* > ( ctx.scratch->take_raw ( sizeof ( storage::value ) * ( plan->group_count + 1 ) ) );

		if ( !keys )
			return fail_run ( ctx, "out of memory" );

		keys [ plan->group_count ] = storage::null_value ( );

		for ( uint32_t i = 0; i < plan->group_count; i++ ) {

			if ( !evaluate ( ctx, &scope, plan->group [ i ], keys [ i ] ) )
				return false;

			if ( keys [ i ].kind == storage::VALUE_REAL && keys [ i ].real == static_cast < double > ( static_cast < int64_t > ( keys [ i ].real ) ) && keys [ i ].real > -9.2e18 && keys [ i ].real < 9.2e18 )
				keys [ i ] = storage::integer_value ( static_cast < int64_t > ( keys [ i ].real ) );

			if ( plan->group [ i ]->nocase )
				keys [ i ] = folded_value ( *ctx.scratch, keys [ i ] );

		}

		uint32_t hash = hash_row ( keys, plan->group_count );
		group_state* group = nullptr;

		if ( table.bucket_count )
			for ( group = table.buckets [ hash & ( table.bucket_count - 1 ) ]; group; group = group->chain )
				if ( group->hash == hash && same_row ( group->keys, keys, plan->group_count ) )
					break;

		if ( !group ) {

			group = new_group ( ctx, scope, table, keys, hash );

			if ( !group )
				return false;

			take_snapshot ( scope, *group );

		}

		return step_group ( ctx, scope, *group );

	}

	static storage::value row_identity ( const source_state& source ) {

		if ( source.plan->query || source.plan->working )
			return storage::integer_value ( static_cast < int64_t > ( source.row_at ) - 1 );

		return storage::integer_value ( source.rowid );

	}

	static bool note_match ( run_context& ctx, source_state& source ) {

		storage::value identity = row_identity ( source );
		bool added = false;

		if ( !set_add ( *ctx.memory, source.matched, &identity, 1, added ) )
			return fail_run ( ctx, "out of memory" );

		return true;

	}

	static bool run_level ( run_context& ctx, query_state& scope, uint32_t level, output_pipe& pipe, group_table& groups );

	static bool run_unmatched ( run_context& ctx, query_state& scope, uint32_t level, output_pipe& pipe, group_table& groups ) {

		source_state& source = scope.sources [ level ];
		prepared_source* real = source.plan;
		prepared_source scan = *real;

		scan.path = { };
		scan.path.kind = PATH_SCAN;
		source.plan = &scan;

		for ( uint32_t s = 0; s < level; s++ ) {

			scope.sources [ s ].null_row = true;
			scope.sources [ s ].values = nullptr;

		}

		bool fine = open_source ( ctx, &scope, source );
		base::arena_mark mark = ctx.scratch->mark ( );

		while ( fine && !pipe.stopped ) {

			bool found = false;

			fine = next_source ( ctx, &scope, source, found );

			if ( !fine || !found )
				break;

			storage::value identity = row_identity ( source );
			bool keep = !set_has ( source.matched, &identity, 1 );

			if ( keep )
				fine = passes ( ctx, &scope, real->filters, real->filter_count, keep );

			if ( fine && keep )
				fine = run_level ( ctx, scope, level + 1, pipe, groups );

			ctx.scratch->rewind ( mark );

		}

		source.plan = real;

		for ( uint32_t s = 0; s < level; s++ )
			scope.sources [ s ].null_row = false;

		return fine;

	}

	static bool run_level ( run_context& ctx, query_state& scope, uint32_t level, output_pipe& pipe, group_table& groups ) {

		prepared_select* plan = scope.plan;

		if ( level == plan->source_count ) {

			if ( plan->grouping )
				return group_step ( ctx, scope, groups );

			if ( scope.captured )
				return capture_window_row ( ctx, scope, *scope.captured );

			return output_row ( ctx, scope, pipe, nullptr );

		}

		source_state& source = scope.sources [ level ];
		prepared_source& definition = plan->sources [ level ];

		if ( !open_source ( ctx, &scope, source ) )
			return false;

		base::arena_mark mark = ctx.scratch->mark ( );
		bool matched = false;

		for ( ;; ) {

			bool found = false;

			if ( !next_source ( ctx, &scope, source, found ) )
				return false;

			if ( !found )
				break;

			bool fine = true;

			if ( ( definition.left || definition.right ) && !passes ( ctx, &scope, definition.on_terms, definition.on_count, fine ) )
				return false;

			if ( fine && definition.right && !note_match ( ctx, source ) )
				return false;

			if ( fine ) {

				sql::expression* const* terms = nullptr;
				uint32_t term_count = 0;

				matched = true;
				source_filters ( source, terms, term_count );

				if ( !passes ( ctx, &scope, terms, term_count, fine ) )
					return false;

				if ( fine && !run_level ( ctx, scope, level + 1, pipe, groups ) )
					return false;

			}

			ctx.scratch->rewind ( mark );

			if ( pipe.stopped )
				return true;

		}

		if ( definition.left && !matched ) {

			bool fine = true;

			source.null_row = true;
			source.values = nullptr;

			if ( !passes ( ctx, &scope, definition.filters, definition.filter_count, fine ) )
				return false;

			if ( fine && !run_level ( ctx, scope, level + 1, pipe, groups ) )
				return false;

			source.null_row = false;
			ctx.scratch->rewind ( mark );

		}

		return true;

	}

	static int32_t compare_groups ( void* context, const void* a, const void* b ) {

		uint32_t count = *static_cast < const uint32_t* > ( context );
		const group_state& x = *static_cast < const group_state* > ( a );
		const group_state& y = *static_cast < const group_state* > ( b );

		for ( uint32_t k = 0; k < count; k++ ) {

			int32_t order = order_values ( x.keys [ k ], y.keys [ k ] );

			if ( order )
				return order;

		}

		return 0;

	}

	static bool output_groups ( run_context& ctx, query_state& scope, output_pipe& pipe, group_table& groups ) {

		prepared_select* plan = scope.plan;

		if ( !groups.count && !plan->group_count ) {

			storage::value none;

			if ( !new_group ( ctx, scope, groups, &none, 0 ) )
				return false;

		}

		group_state** ordered = ctx.memory->make < group_state* > ( groups.count + 1 );

		if ( !ordered )
			return fail_run ( ctx, "out of memory" );

		uint32_t count = 0;

		for ( group_state* group = groups.first; group; group = group->next )
			ordered [ count++ ] = group;

		if ( plan->group_count && !base::sort_pointers ( reinterpret_cast < void** > ( ordered ), count, compare_groups, &plan->group_count ) )
			return fail_run ( ctx, "out of memory while sorting groups" );

		storage::value* finished = ctx.memory->make < storage::value > ( plan->aggregate_count + 1 );

		if ( !finished )
			return fail_run ( ctx, "out of memory" );

		scope.aggregate_values = finished;

		for ( uint32_t i = 0; i < count && !pipe.stopped; i++ ) {

			group_state& group = *ordered [ i ];
			base::arena_mark mark = ctx.scratch->mark ( );

			for ( uint32_t a = 0; a < plan->aggregate_count; a++ )
				if ( !finish_aggregate ( ctx, group.states [ a ], finished [ a ] ) )
					return false;

			if ( !restore_snapshot ( ctx, scope, group ) )
				return false;

			bool fine = true;

			if ( plan->having ) {

				truth_kind truth = TRUTH_NULL;

				if ( !condition ( ctx, &scope, plan->having, truth ) )
					return false;

				fine = truth == TRUTH_TRUE;

			}

			if ( fine && ( scope.captured ? !capture_window_row ( ctx, scope, *scope.captured ) : !output_row ( ctx, scope, pipe, nullptr ) ) )
				return false;

			ctx.scratch->rewind ( mark );

		}

		scope.aggregate_values = nullptr;

		return true;

	}

	static bool run_core ( run_context& ctx, prepared_select* plan, query_state* parent, output_pipe& pipe ) {

		query_state scope = { };

		scope.plan = plan;
		scope.parent = parent;

		if ( plan->source_count ) {

			scope.sources = ctx.memory->make < source_state > ( plan->source_count );

			if ( !scope.sources )
				return fail_run ( ctx, "out of memory" );

			for ( uint32_t i = 0; i < plan->source_count; i++ )
				scope.sources [ i ].plan = &plan->sources [ i ];

		}

		if ( plan->values_only ) {

			const sql::select_statement* syntax = plan->syntax;

			for ( uint32_t r = 0; r < syntax->value_rows && !pipe.stopped; r++ ) {

				base::arena_mark mark = ctx.scratch->mark ( );
				storage::value* values = ctx.scratch->make < storage::value > ( plan->output_count + 1 );

				if ( !values )
					return fail_run ( ctx, "out of memory" );

				for ( uint32_t c = 0; c < plan->output_count; c++ )
					if ( !evaluate ( ctx, &scope, syntax->values [ r * syntax->value_columns + c ], values [ c ] ) )
						return false;

				if ( !output_row ( ctx, scope, pipe, values ) )
					return false;

				ctx.scratch->rewind ( mark );

			}

			return true;

		}

		group_table groups = { };
		window_rows captured = { };
		bool fine = true;
		base::arena_mark mark = ctx.scratch->mark ( );

		if ( plan->window_count )
			scope.captured = &captured;

		if ( !passes ( ctx, &scope, plan->constant_filters, plan->constant_count, fine ) )
			return false;

		ctx.scratch->rewind ( mark );

		if ( plan->source_count == 1 && plan->sources [ 0 ].table && !plan->grouping && !plan->window_count && pipe.sorting && pipe.bound > 0 && !pipe.distinct && pipe.key_count == 1 ) {

			const sql::expression* key = plan->order_output && plan->order_output [ 0 ] >= 0 ? plan->outputs [ plan->order_output [ 0 ] ] : plan->order_values [ 0 ];

			if ( key && key->kind == sql::EXPRESSION_COLUMN && key->depth == 0 && key->source == 0 ) {

				scope.sources [ 0 ].early_pipe = &pipe;
				scope.sources [ 0 ].early_column = key->column;

			}

		}

		bool folded = fine && can_fold ( plan );

		if ( folded && !fold_rows ( ctx, scope, groups ) )
			return false;

		if ( fine && !folded && !run_level ( ctx, scope, 0, pipe, groups ) )
			return false;

		for ( uint32_t s = 0; fine && s < plan->source_count && !pipe.stopped; s++ )
			if ( plan->sources [ s ].right && !run_unmatched ( ctx, scope, s, pipe, groups ) )
				return false;

		if ( plan->grouping && !output_groups ( ctx, scope, pipe, groups ) )
			return false;

		if ( !plan->window_count )
			return true;

		storage::value* values = nullptr;

		if ( !compute_windows ( ctx, scope, captured, values ) )
			return false;

		scope.captured = nullptr;

		for ( uint32_t i = 0; i < captured.count && !pipe.stopped; i++ ) {

			base::arena_mark row_mark = ctx.scratch->mark ( );

			if ( !restore_window_row ( ctx, scope, *captured.rows [ i ] ) )
				return false;

			scope.window_values = values + static_cast < size_t > ( i ) * plan->window_count;

			if ( !output_row ( ctx, scope, pipe, nullptr ) )
				return false;

			ctx.scratch->rewind ( row_mark );

		}

		scope.window_values = nullptr;
		scope.aggregate_values = nullptr;

		return true;

	}

	static bool collect_emit ( void* context, const storage::value* values, uint32_t count ) {

		collected_rows& rows = *static_cast < collected_rows* > ( context );

		if ( rows.count == rows.room ) {

			uint32_t grown = rows.room ? rows.room * 2 : 4;
			storage::value* made = static_cast < storage::value* > ( rows.memory->take_raw ( sizeof ( storage::value ) * grown * ( rows.width ? rows.width : 1 ) ) );

			if ( !made )
				return false;

			if ( rows.values )
				base::copy ( made, rows.values, sizeof ( storage::value ) * rows.count * rows.width );

			rows.values = made;
			rows.room = grown;

		}

		storage::value* at = rows.values + static_cast < size_t > ( rows.count ) * rows.width;

		for ( uint32_t i = 0; i < count; i++ ) {

			at [ i ] = keep_value ( *rows.memory, values [ i ] );

			if ( at [ i ].kind == storage::VALUE_NULL && values [ i ].kind != storage::VALUE_NULL )
				return false;

		}

		rows.count++;

		return true;

	}

	static bool limit_value ( run_context& ctx, query_state* parent, const sql::expression* item, int64_t fallback, int64_t& out ) {

		out = fallback;

		if ( !item )
			return true;

		storage::value value;

		if ( !evaluate ( ctx, parent, item, value ) )
			return false;

		storage::value number = apply_affinity ( *ctx.scratch, value, sql::AFFINITY_INTEGER );

		if ( number.kind != storage::VALUE_INTEGER )
			return fail_run ( ctx, "LIMIT and OFFSET need whole numbers" );

		out = number.integer;

		return true;

	}

	static void start_pipe ( output_pipe& pipe, prepared_select* plan, row_sink* sink, int64_t skip, int64_t remaining ) {

		pipe = { };
		pipe.width = plan->output_count;
		pipe.key_count = plan->order_by_index ? 0 : plan->order_count;
		pipe.descending = plan->order_desc;
		pipe.nocase = plan->order_nocase;
		pipe.outputs = plan->outputs;
		pipe.sorting = plan->order_count > 0 && !plan->order_by_index;
		pipe.sink = sink;
		pipe.skip = skip > 0 ? skip : 0;
		pipe.remaining = remaining < 0 ? -1 : remaining;
		pipe.bound = pipe.sorting && pipe.remaining >= 0 && pipe.skip + pipe.remaining <= SORT_BOUND ? pipe.skip + pipe.remaining : -1;

	}

	static bool run_compound ( run_context& ctx, prepared_select* first, query_state* parent, output_pipe& final_pipe ) {

		collected_rows result = { };

		result.memory = ctx.memory;
		result.width = first->output_count;

		row_sink collect = { &result, collect_emit };
		output_pipe pipe = { };

		start_pipe ( pipe, first, &collect, 0, -1 );
		pipe.sorting = false;
		pipe.key_count = 0;
		pipe.distinct = first->distinct;

		if ( !run_core ( ctx, first, parent, pipe ) )
			return false;

		for ( prepared_select* core = first; core->next; core = core->next ) {

			collected_rows right = { };

			right.memory = ctx.memory;
			right.width = first->output_count;

			row_sink take = { &right, collect_emit };

			start_pipe ( pipe, core->next, &take, 0, -1 );
			pipe.sorting = false;
			pipe.key_count = 0;
			pipe.distinct = core->next->distinct;

			if ( !run_core ( ctx, core->next, parent, pipe ) )
				return false;

			uint32_t width = result.width;
			collected_rows merged = { };
			value_set seen = { };
			value_set other = { };

			merged.memory = ctx.memory;
			merged.width = width;

			if ( core->compound == sql::COMPOUND_UNION_ALL ) {

				for ( uint32_t r = 0; r < right.count; r++ )
					if ( !collect_emit ( &result, right.values + static_cast < size_t > ( r ) * width, width ) )
						return fail_run ( ctx, "out of memory" );

				continue;

			}

			if ( core->compound != sql::COMPOUND_UNION ) {

				for ( uint32_t r = 0; r < right.count; r++ ) {

					bool added = false;

					if ( !set_add ( *ctx.memory, other, right.values + static_cast < size_t > ( r ) * width, width, added ) )
						return fail_run ( ctx, "out of memory" );

				}

			}

			for ( uint32_t side = 0; side < ( core->compound == sql::COMPOUND_UNION ? 2u : 1u ); side++ ) {

				collected_rows& from = side ? right : result;

				for ( uint32_t r = 0; r < from.count; r++ ) {

					const storage::value* row = from.values + static_cast < size_t > ( r ) * width;
					bool added = false;

					if ( core->compound == sql::COMPOUND_INTERSECT && !set_has ( other, row, width ) )
						continue;

					if ( core->compound == sql::COMPOUND_EXCEPT && set_has ( other, row, width ) )
						continue;

					if ( !set_add ( *ctx.memory, seen, row, width, added ) )
						return fail_run ( ctx, "out of memory" );

					if ( added && !collect_emit ( &merged, row, width ) )
						return fail_run ( ctx, "out of memory" );

				}

			}

			result = merged;

		}

		query_state scope = { };

		scope.plan = first;
		scope.parent = parent;

		for ( uint32_t r = 0; r < result.count && !final_pipe.stopped; r++ ) {

			base::arena_mark mark = ctx.scratch->mark ( );

			if ( !output_row ( ctx, scope, final_pipe, result.values + static_cast < size_t > ( r ) * result.width ) )
				return false;

			ctx.scratch->rewind ( mark );

		}

		return true;

	}

	bool run_plan ( run_context& ctx, prepared_select* plan, query_state* parent, row_sink& sink ) {

		if ( ++ctx.depth > RUN_DEPTH * 4 ) {

			ctx.depth--;

			return fail_run ( ctx, "the query nests too deeply" );

		}

		int64_t remaining = -1;
		int64_t skip = 0;
		output_pipe pipe;
		bool fine = limit_value ( ctx, parent, plan->limit, -1, remaining ) && limit_value ( ctx, parent, plan->offset, 0, skip );

		if ( fine ) {

			start_pipe ( pipe, plan, &sink, skip, remaining );

			if ( plan->next )
				fine = run_compound ( ctx, plan, parent, pipe );
			else {

				pipe.distinct = plan->distinct;
				fine = run_core ( ctx, plan, parent, pipe );

			}

			fine = fine && finish_pipe ( ctx, pipe );

		}

		ctx.depth--;

		return fine;

	}

	bool collect_plan ( run_context& ctx, prepared_select* plan, query_state* parent, collected_rows& out ) {

		out.width = plan->output_count;

		if ( !out.memory )
			out.memory = ctx.memory;

		row_sink sink = { &out, collect_emit };

		if ( !run_plan ( ctx, plan, parent, sink ) )
			return false;

		if ( out.count && !out.values )
			return fail_run ( ctx, "out of memory" );

		return true;

	}

	bool collect_recursive ( run_context& ctx, cte_entry& cte, query_state* parent, collected_rows& out ) {

		uint32_t width = cte.width;
		value_set seen = { };
		int64_t limit = -1;
		collected_rows fresh = { };

		out.width = width;

		if ( !out.memory )
			out.memory = ctx.memory;

		if ( !limit_value ( ctx, parent, cte.plan->limit, -1, limit ) )
			return false;

		auto take = [ & ] ( collected_rows& from, collected_rows& kept ) {

			for ( uint32_t r = 0; r < from.count; r++ ) {

				const storage::value* row = from.values + static_cast < size_t > ( r ) * width;
				bool added = true;

				if ( limit >= 0 && out.count >= static_cast < uint64_t > ( limit ) )
					return true;

				if ( cte.distinct && !set_add ( *ctx.memory, seen, row, width, added ) )
					return fail_run ( ctx, "out of memory" );

				if ( !added )
					continue;

				if ( !collect_emit ( &out, row, width ) || !collect_emit ( &kept, row, width ) )
					return fail_run ( ctx, "out of memory" );

			}

			return true;

		};

		auto run_part = [ & ] ( bool working, collected_rows& into ) {

			into = { };
			into.memory = ctx.memory;
			into.width = width;

			row_sink sink = { &into, collect_emit };

			for ( prepared_select* core = cte.plan; core; core = core->next ) {

				if ( core->uses_working != working )
					continue;

				output_pipe pipe;

				start_pipe ( pipe, core, &sink, 0, -1 );
				pipe.sorting = false;
				pipe.key_count = 0;
				pipe.distinct = core->distinct;

				if ( !run_core ( ctx, core, parent, pipe ) )
					return false;

			}

			return true;

		};

		collected_rows working = { };

		working.memory = ctx.memory;
		working.width = width;

		if ( !run_part ( false, fresh ) || !take ( fresh, working ) )
			return false;

		for ( uint32_t round = 0; working.count; round++ ) {

			if ( out.count > 50000000 || round > 10000000 )
				return fail_named ( ctx, "the recursive WITH query ", cte.syntax->name, " made too many rows, add a WHERE or a LIMIT that stops it" );

			if ( limit >= 0 && out.count >= static_cast < uint64_t > ( limit ) )
				break;

			cte.working = working.values;
			cte.working_count = working.count;

			collected_rows next = { };

			if ( !run_part ( true, next ) )
				return false;

			working = { };
			working.memory = ctx.memory;
			working.width = width;

			if ( !take ( next, working ) )
				return false;

		}

		cte.working = nullptr;
		cte.working_count = 0;

		return true;

	}

	static bool first_row_emit ( void* context, const storage::value* values, uint32_t count ) {

		first_row_sink& sink = *static_cast < first_row_sink* > ( context );

		sink.seen = true;

		if ( count ) {

			sink.value = values [ 0 ];

			if ( sink.value.kind == storage::VALUE_TEXT || sink.value.kind == storage::VALUE_BLOB ) {

				sink.bytes.clear ( );
				sink.bytes.add ( reinterpret_cast < const char* > ( sink.value.bytes ), sink.value.size );
				sink.value.bytes = reinterpret_cast < const uint8_t* > ( sink.bytes.value ( ) );

			}

		}

		return false;

	}

	static bool member_emit ( void* context, const storage::value* values, uint32_t count ) {

		member_sink& sink = *static_cast < member_sink* > ( context );
		storage::value probe = sink.probe;
		storage::value candidate = values [ 0 ];

		sink.any = true;
		comparison_affinity ( *sink.ctx->scratch, sink.affinity, sql::AFFINITY_ANY, probe, candidate );

		truth_kind truth = compare_with ( sql::OPERATOR_EQUAL, probe, candidate );

		if ( truth == TRUTH_TRUE ) {

			sink.found = true;

			return false;

		}

		if ( truth == TRUTH_NULL )
			sink.has_null = true;

		return count != 0;

	}

	static bool set_emit ( void* context, const storage::value* values, uint32_t count ) {

		set_sink& sink = *static_cast < set_sink* > ( context );
		bool added = false;
		storage::value single;

		if ( count == 1 ) {

			storage::value unused = storage::null_value ( );

			single = values [ 0 ];
			comparison_affinity ( *sink.memory, sink.left, sink.right, unused, single );
			values = &single;

		}

		if ( !set_add ( *sink.memory, *sink.set, values, count, added ) ) {

			sink.failed = true;

			return false;

		}

		return true;

	}

	static cached_child* find_cache ( run_context& ctx, const sql::expression* item ) {

		for ( cached_child* entry = ctx.cache; entry; entry = entry->next )
			if ( entry->key == item )
				return entry;

		return nullptr;

	}

	static bool membership ( run_context& ctx, const sql::expression* item, const storage::value& probe, const value_set& set, storage::value& out ) {

		truth_kind result = TRUTH_FALSE;

		if ( !set.count )
			result = TRUTH_FALSE;
		else if ( probe.kind == storage::VALUE_NULL )
			result = TRUTH_NULL;
		else {

			const prepared_select* child = item->query ? static_cast < const prepared_select* > ( item->query->prepared ) : nullptr;
			sql::affinity_kind left = expression_affinity ( item->left );
			sql::affinity_kind right = child && child->output_count ? expression_affinity ( child->outputs [ 0 ] ) : sql::AFFINITY_ANY;
			storage::value key = seek_value ( *ctx.scratch, left, probe );
			storage::value turned = probe;
			storage::value unused = storage::null_value ( );

			comparison_affinity ( *ctx.scratch, left, right, turned, unused );

			if ( set_has ( set, &key, 1 ) || set_has ( set, &probe, 1 ) || set_has ( set, &turned, 1 ) )
				result = TRUTH_TRUE;
			else if ( set.has_null )
				result = TRUTH_NULL;

		}

		if ( item->negated )
			result = result == TRUTH_NULL ? TRUTH_NULL : result == TRUTH_TRUE ? TRUTH_FALSE : TRUTH_TRUE;

		out = truth_value ( result );

		return true;

	}

	bool run_child ( run_context& ctx, query_state* scope, const sql::expression* item, storage::value& out ) {

		prepared_select* child = static_cast < prepared_select* > ( item->query->prepared );

		if ( !child )
			return fail_run ( ctx, "a subquery was not prepared" );

		if ( item->kind == sql::EXPRESSION_EXISTS && child->correlated ) {

			semi_plan* semi = semi_join ( ctx, child );

			if ( semi )
				return semi_exists ( ctx, scope, item, *semi, out );

		}

		cached_child* cached = child->correlated ? nullptr : find_cache ( ctx, item );

		if ( cached ) {

			if ( item->kind == sql::EXPRESSION_SUBQUERY )
				out = cached->scalar;
			else if ( item->kind == sql::EXPRESSION_EXISTS )
				out = storage::integer_value ( cached->exists != item->negated ? 1 : 0 );
			else {

				storage::value probe;

				if ( !evaluate ( ctx, scope, item->left, probe ) )
					return false;

				return membership ( ctx, item, probe, cached->set, out );

			}

			return true;

		}

		base::arena_mark mark = ctx.memory->mark ( );
		bool fine = true;

		if ( item->kind == sql::EXPRESSION_IN && child->correlated ) {

			member_sink sink = { };
			storage::value probe;

			if ( !evaluate ( ctx, scope, item->left, probe ) )
				return false;

			sink.ctx = &ctx;
			sink.probe = keep_value ( *ctx.scratch, probe );
			sink.affinity = expression_affinity ( item->left );

			row_sink rows = { &sink, member_emit };

			fine = run_plan ( ctx, child, scope, rows );
			ctx.memory->rewind ( mark );

			if ( !fine )
				return false;

			truth_kind result = TRUTH_FALSE;

			if ( sink.found )
				result = TRUTH_TRUE;
			else if ( sink.any && ( sink.has_null || probe.kind == storage::VALUE_NULL ) )
				result = TRUTH_NULL;

			if ( item->negated )
				result = result == TRUTH_NULL ? TRUTH_NULL : result == TRUTH_TRUE ? TRUTH_FALSE : TRUTH_TRUE;

			out = truth_value ( result );

			return true;

		}

		if ( item->kind == sql::EXPRESSION_IN ) {

			cached_child* entry = ctx.results->make < cached_child > ( 1 );

			if ( !entry )
				return fail_run ( ctx, "out of memory" );

			set_sink sink = { ctx.results, &entry->set, false, expression_affinity ( item->left ), child->output_count ? expression_affinity ( child->outputs [ 0 ] ) : sql::AFFINITY_ANY };
			row_sink rows = { &sink, set_emit };

			fine = run_plan ( ctx, child, scope, rows );
			ctx.memory->rewind ( mark );

			if ( sink.failed )
				return fail_run ( ctx, "out of memory" );

			if ( !fine )
				return false;

			entry->key = item;
			entry->next = ctx.cache;
			ctx.cache = entry;

			storage::value probe;

			if ( !evaluate ( ctx, scope, item->left, probe ) )
				return false;

			return membership ( ctx, item, probe, entry->set, out );

		}

		first_row_sink sink;

		sink.value = storage::null_value ( );
		sink.seen = false;

		row_sink rows = { &sink, first_row_emit };

		fine = run_plan ( ctx, child, scope, rows );
		ctx.memory->rewind ( mark );

		if ( !fine )
			return false;

		storage::value result = item->kind == sql::EXPRESSION_EXISTS ? storage::integer_value ( sink.seen != item->negated ? 1 : 0 ) : sink.value;

		if ( !child->correlated ) {

			cached_child* entry = ctx.results->make < cached_child > ( 1 );

			if ( !entry )
				return fail_run ( ctx, "out of memory" );

			entry->key = item;
			entry->scalar = keep_value ( *ctx.results, sink.value );
			entry->exists = sink.seen;
			entry->next = ctx.cache;
			ctx.cache = entry;

			out = item->kind == sql::EXPRESSION_EXISTS ? result : entry->scalar;

			return true;

		}

		out = keep_value ( *ctx.scratch, result );

		return true;

	}

}
