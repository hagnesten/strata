#include "engine/run/run.hh"

namespace engine {

	static void order_generated ( const table_info& table, uint32_t column, uint8_t* state, uint32_t* order, uint32_t& count );

	static void order_uses ( const table_info& table, const sql::expression* item, uint8_t* state, uint32_t* order, uint32_t& count ) {

		if ( !item )
			return;

		if ( item->kind == sql::EXPRESSION_COLUMN && item->depth == 0 && item->column >= 0 && static_cast < uint32_t > ( item->column ) < table.column_count && table.columns [ item->column ].generated )
			order_generated ( table, static_cast < uint32_t > ( item->column ), state, order, count );

		order_uses ( table, item->left, state, order, count );
		order_uses ( table, item->right, state, order, count );
		order_uses ( table, item->third, state, order, count );

		for ( uint32_t i = 0; i < item->count; i++ )
			order_uses ( table, item->list [ i ], state, order, count );

	}

	static void order_generated ( const table_info& table, uint32_t column, uint8_t* state, uint32_t* order, uint32_t& count ) {

		if ( state [ column ] )
			return;

		state [ column ] = 1;
		order_uses ( table, table.columns [ column ].generated, state, order, count );
		state [ column ] = 2;
		order [ count++ ] = column;

	}

	table_scope* scope_for ( run_context& ctx, table_info& table ) {

		for ( table_scope* entry = ctx.scopes; entry; entry = entry->next )
			if ( entry->table == &table )
				return entry;

		table_scope* entry = ctx.lasting->make < table_scope > ( 1 );
		prepared_select* plan = prepare_table_scope ( ctx, &table, table.name, nullptr, nullptr );

		if ( !entry || !plan ) {

			fail_run ( ctx, "out of memory" );

			return nullptr;

		}

		entry->table = &table;
		entry->plan = plan;
		entry->state.plan = plan;
		entry->state.sources = ctx.lasting->make < source_state > ( 1 );

		if ( !entry->state.sources ) {

			fail_run ( ctx, "out of memory" );

			return nullptr;

		}

		entry->state.sources [ 0 ].plan = &plan->sources [ 0 ];

		for ( uint32_t i = 0; i < table.check_count; i++ )
			if ( !bind_expression ( ctx, plan, table.checks [ i ], BIND_PLAIN ) )
				return nullptr;

		if ( table.generated_count ) {

			uint8_t* state = ctx.lasting->make < uint8_t > ( table.column_count );

			entry->generated_order = ctx.lasting->make < uint32_t > ( table.column_count );

			if ( !state || !entry->generated_order ) {

				fail_run ( ctx, "out of memory" );

				return nullptr;

			}

			for ( uint32_t i = 0; i < table.column_count; i++ )
				if ( table.columns [ i ].generated && !bind_expression ( ctx, plan, table.columns [ i ].generated, BIND_PLAIN ) )
					return nullptr;

			for ( uint32_t i = 0; i < table.column_count; i++ )
				if ( table.columns [ i ].generated )
					order_generated ( table, i, state, entry->generated_order, entry->generated_count );

		}

		entry->next = ctx.scopes;
		ctx.scopes = entry;

		return entry;

	}

	bool compute_generated ( run_context& ctx, const table_info& table, storage::value* values, int64_t rowid, uint32_t from ) {

		if ( !table.generated_count )
			return true;

		table_scope* scope = scope_for ( ctx, const_cast < table_info& > ( table ) );

		if ( !scope )
			return false;

		source_state& source = scope->state.sources [ 0 ];
		storage::value* saved_values = source.values;
		int64_t saved_rowid = source.rowid;
		bool saved_null = source.null_row;
		bool fine = true;

		source.values = values;
		source.rowid = rowid;
		source.null_row = false;

		for ( uint32_t i = 0; i < scope->generated_count && fine; i++ ) {

			uint32_t column = scope->generated_order [ i ];
			storage::value out;

			if ( column < from )
				continue;

			fine = evaluate ( ctx, &scope->state, table.columns [ column ].generated, out );

			if ( fine )
				fine = column_value ( ctx, table.columns [ column ], out, values [ column ] );

		}

		source.values = saved_values;
		source.rowid = saved_rowid;
		source.null_row = saved_null;

		return fine;

	}

}