#include "engine/database/database.hh"

namespace engine {

	const char* returning_alias ( const sql::statement* item ) {

		return item->kind == sql::STATEMENT_UPDATE ? item->update->alias : item->kind == sql::STATEMENT_DELETE ? item->remove->alias : nullptr;

	}

	const sql::returning_clause* returning_of ( const sql::statement* item, const char*& table ) {

		switch ( item->kind ) {

			case sql::STATEMENT_INSERT:
				table = item->insert->table;
				return &item->insert->returning;

			case sql::STATEMENT_UPDATE:
				table = item->update->table;
				return &item->update->returning;

			case sql::STATEMENT_DELETE:
				table = item->remove->table;
				return &item->remove->returning;

			default:
				table = nullptr;
				return nullptr;

		}

	}

	static const char* bare_name ( const char* name ) {

		const char* bare = name;

		for ( const char* at = name; *at; at++ )
			if ( *at == '.' )
				bare = at + 1;

		return bare;

	}

	static sql::expression* column_reference ( run_context& ctx, const char* name ) {

		sql::expression* made = ctx.lasting->make < sql::expression > ( 1 );

		if ( !made )
			return nullptr;

		*made = { };
		made->kind = sql::EXPRESSION_COLUMN;
		made->name = name;
		made->source = -1;
		made->column = -1;
		made->aggregate = -1;
		made->window_index = -1;

		return made;

	}

	static const char* returning_name ( run_context& ctx, const sql::result_column& column ) {

		if ( column.alias )
			return column.alias;

		if ( column.value->kind == sql::EXPRESSION_COLUMN && column.value->name )
			return column.value->name;

		if ( column.value->kind == sql::EXPRESSION_FUNCTION && column.value->name )
			return column.value->name;

		return column.written && *column.written ? column.written : "?column?";

	}

	bool has_returning ( const sql::statement* item ) {

		const char* table = nullptr;
		const sql::returning_clause* clause = returning_of ( item, table );

		return clause && clause->count;

	}

	returning_plan* plan_returning ( run_context& ctx, const char* name, const char* alias, const sql::returning_clause& returning ) {

		const sql::returning_clause* clause = &returning;

		if ( !clause->count )
			return nullptr;

		if ( clause->prepared )
			return static_cast < returning_plan* > ( clause->prepared );

		table_info* table = find_table ( *ctx.schema, name );

		if ( !table ) {

			fail_named ( ctx, "no such table: ", name, "" );

			return nullptr;

		}

		returning_plan* made = ctx.lasting->make < returning_plan > ( 1 );
		uint32_t width = 0;

		for ( uint32_t i = 0; i < clause->count; i++ )
			width += clause->columns [ i ].star ? table->column_count : 1;

		if ( !made ) {

			fail_run ( ctx, "out of memory" );

			return nullptr;

		}

		*made = { };
		made->scope = prepare_table_scope ( ctx, table, alias ? alias : bare_name ( table->name ), nullptr, nullptr );
		made->outputs = ctx.lasting->make < sql::expression* > ( width + 1 );
		made->names = ctx.lasting->make < const char* > ( width + 1 );

		if ( !made->scope )
			return nullptr;

		if ( !made->outputs || !made->names ) {

			fail_run ( ctx, "out of memory" );

			return nullptr;

		}

		for ( uint32_t i = 0; i < clause->count; i++ ) {

			const sql::result_column& column = clause->columns [ i ];

			if ( column.star ) {

				for ( uint32_t c = 0; c < table->column_count; c++ ) {

					sql::expression* reference = column_reference ( ctx, table->columns [ c ].name );

					if ( !reference ) {

						fail_run ( ctx, "out of memory" );

						return nullptr;

					}

					made->outputs [ made->count ] = reference;
					made->names [ made->count++ ] = table->columns [ c ].name;

				}

				continue;

			}

			made->outputs [ made->count ] = column.value;
			made->names [ made->count++ ] = returning_name ( ctx, column );

		}

		for ( uint32_t i = 0; i < made->count; i++ )
			if ( !bind_expression ( ctx, made->scope, made->outputs [ i ], BIND_PLAIN ) )
				return nullptr;

		made->scope->outputs = made->outputs;
		made->scope->names = made->names;
		made->scope->output_count = made->count;
		clause->prepared = made;

		return made;

	}

	void start_returning ( run_context& ctx, returning_plan* plan ) {

		if ( plan && ctx.output && ctx.output->columns )
			ctx.output->columns ( ctx.output->context, plan->names, plan->count );

	}

	bool emit_returning ( run_context& ctx, returning_plan* plan, const storage::value* values, int64_t rowid ) {

		if ( !plan || !ctx.output )
			return true;

		query_sink* sink = ctx.output;
		query_state state = { };
		source_state source = { };
		base::arena_mark mark = ctx.scratch->mark ( );
		storage::value* row = ctx.scratch->make < storage::value > ( plan->count + 1 );

		if ( !row )
			return fail_run ( ctx, "out of memory" );

		const table_info* table = plan->scope->sources [ 0 ].table;
		storage::value* current = ctx.scratch->make < storage::value > ( table->column_count + 1 );

		if ( !current )
			return fail_run ( ctx, "out of memory" );

		base::copy ( current, values, sizeof ( storage::value ) * table->column_count );

		if ( table->rowid_column >= 0 )
			current [ table->rowid_column ] = storage::integer_value ( rowid );

		source.plan = &plan->scope->sources [ 0 ];
		source.values = current;
		source.rowid = rowid;
		state.plan = plan->scope;
		state.sources = &source;

		for ( uint32_t i = 0; i < plan->count; i++ )
			if ( !evaluate ( ctx, &state, plan->outputs [ i ], row [ i ] ) )
				return false;

		bool fine = !sink->row || sink->row ( sink->context, row, plan->count );

		ctx.scratch->rewind ( mark );

		return fine || fail_run ( ctx, "the returned rows could not be delivered" );

	}

}
