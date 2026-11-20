#include "engine/schema/schema.hh"

namespace engine {

	static bool bare_column ( const sql::expression* item ) {

		if ( !item || item->aggregate >= 0 )
			return false;

		if ( item->kind == sql::EXPRESSION_COLUMN && item->depth == 0 )
			return true;

		if ( bare_column ( item->left ) || bare_column ( item->right ) || bare_column ( item->third ) )
			return true;

		for ( uint32_t i = 0; item->list && i < item->count; i++ )
			if ( bare_column ( item->list [ i ] ) )
				return true;

		return false;

	}

	static bool plain_after ( const sql::expression* item ) {

		return !bare_column ( item ) && !uses_query ( item );

	}

	bool can_fold ( const prepared_select* plan ) {

		if ( plan->source_count != 1 || !plan->aggregate_count || plan->group_count || plan->window_count || plan->values_only )
			return false;

		const prepared_source& source = plan->sources [ 0 ];

		if ( !source.table || source.query || source.cte || source.working || source.left || source.right || source.table->generated_count )
			return false;

		if ( source.path.kind != PATH_SCAN && source.path.kind != PATH_ROWID_RANGE )
			return false;

		for ( uint32_t i = 0; i < plan->output_count; i++ )
			if ( !plain_after ( plan->outputs [ i ] ) )
				return false;

		for ( uint32_t i = 0; i < plan->order_count; i++ )
			if ( !plain_after ( plan->order_values [ i ] ) )
				return false;

		if ( plan->having && !plain_after ( plan->having ) )
			return false;

		for ( uint32_t a = 0; a < plan->aggregate_count; a++ )
			if ( uses_query ( plan->aggregates [ a ] ) )
				return false;

		for ( uint32_t t = 0; t < source.filter_count; t++ )
			if ( uses_query ( source.filters [ t ] ) )
				return false;

		return true;

	}

	static storage::scan_outcome fold_row ( void* context, int64_t rowid, const uint8_t* data, size_t size ) {

		fold_state& item = *static_cast < fold_state* > ( context );
		run_context& ctx = *item.ctx;
		source_state& source = *item.source;
		bool pass = true;

		if ( item.range && rowid > item.high )
			return storage::SCAN_STOP;

		base::arena_mark mark = ctx.scratch->mark ( );
		bool fine = !item.checked || quick_passes ( ctx, source, rowid, data, size, pass );

		if ( fine && pass ) {

			fine = decode_into ( ctx, source, rowid, data, size, item.row );
			source.values = item.row;
			source.rowid = rowid;
			source.null_row = false;

			if ( fine && item.term_count )
				fine = passes ( ctx, item.scope, item.terms, item.term_count, pass );

			if ( fine && pass )
				fine = step_group ( ctx, *item.scope, *item.group );

		}

		ctx.scratch->rewind ( mark );

		if ( !fine ) {

			item.failed = true;

			return storage::SCAN_FAIL;

		}

		return storage::SCAN_REJECT;

	}

	bool fold_rows ( run_context& ctx, query_state& scope, group_table& groups ) {

		source_state& source = scope.sources [ 0 ];
		const table_info& table = *source.plan->table;
		storage::value none = storage::null_value ( );
		group_state* group = groups.first ? groups.first : new_group ( ctx, scope, groups, &none, 0 );

		if ( !group || !open_source ( ctx, &scope, source ) )
			return false;

		if ( source.done || !source.at.valid )
			return true;

		if ( !source.quick_compiled ) {

			source.quick_compiled = true;

			if ( !compile_quick ( ctx, &scope, source ) )
				return false;

		}

		storage::value* row = static_cast < storage::value* > ( ctx.scratch->take_raw ( sizeof ( storage::value ) * ( table.column_count + 1 ) ) );

		if ( !row )
			return fail_run ( ctx, "out of memory" );

		fold_state state = { };

		state.ctx = &ctx;
		state.scope = &scope;
		state.source = &source;
		state.group = group;
		state.row = row;
		state.checked = source.quick_ready && source.quick_count;
		state.range = source.plan->path.kind == PATH_ROWID_RANGE;
		state.high = source.high;
		source_filters ( source, state.terms, state.term_count );

		storage::scan_result result;
		bool fine = storage::cursor_scan ( source.at, *ctx.scratch, fold_row, &state, result );

		source.values = nullptr;
		source.done = true;
		ctx.rows_examined += result.examined;

		if ( !fine )
			return state.failed ? false : fail_storage ( ctx, "a table could not be read" );

		return result.outcome != storage::SCAN_FAIL;

	}

}
