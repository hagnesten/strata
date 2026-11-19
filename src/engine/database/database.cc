#include "engine/database/database.hh"

namespace engine {

	bool reload_schema ( database& item ) {

		base::text problem;

		if ( load_catalog ( item.pages, *item.schema, problem ) )
			return true;

		if ( !item.problem.size )
			item.problem.add ( problem.value ( ) );

		return false;

	}

	bool open_database ( database& item, const char* path ) {

		item.problem.clear ( );

		if ( item.open )
			close_database ( item );

		if ( !storage::open_pager ( item.pages, path ) ) {

			item.problem.add ( "the database could not be opened: " ).add ( item.pages.problem );
			storage::close_pager ( item.pages );

			return false;

		}

		item.schema = base::make < catalog > ( 1 );

		if ( !item.schema ) {

			item.problem.add ( "out of memory" );
			storage::close_pager ( item.pages );

			return false;

		}

		if ( !load_catalog ( item.pages, *item.schema, item.problem ) ) {

			clear_catalog ( *item.schema );
			base::release ( item.schema );
			item.schema = nullptr;
			storage::close_pager ( item.pages );

			return false;

		}

		item.random = 0x9E3779B97F4A7C15ull ^ reinterpret_cast < uint64_t > ( &item ) ^ ( item.pages.header.change * 0xBF58476D1CE4E5B9ull );

		if ( !item.random )
			item.random = 1;

		item.open = true;
		item.in_transaction = false;

		return true;

	}

	void close_database ( database& item ) {

		if ( !item.open )
			return;

		if ( item.in_transaction )
			storage::rollback_pager ( item.pages );

		forget_savepoints ( item );
		storage::close_pager ( item.pages );

		if ( item.schema ) {

			clear_catalog ( *item.schema );
			base::release ( item.schema );
			item.schema = nullptr;

		}

		item.open = false;
		item.in_transaction = false;

	}

	void describe_value ( base::text& out, const storage::value& item ) {

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

			case storage::VALUE_TEXT:
				out.add ( reinterpret_cast < const char* > ( item.bytes ), item.size );
				break;

			case storage::VALUE_BLOB:
				out.add ( "X'" );

				for ( size_t i = 0; i < item.size; i++ )
					out.hex ( item.bytes [ i ], 2, true );

				out.add ( '\'' );
				break;

		}

	}

	static bool forward_row ( void* context, const storage::value* values, uint32_t count ) {

		query_sink* sink = static_cast < query_sink* > ( context );

		return !sink || !sink->row || sink->row ( sink->context, values, count );

	}

	static bool announce ( query_sink* sink, const char* const* names, uint32_t count ) {

		return !sink || !sink->columns || sink->columns ( sink->context, names, count );

	}

	static bool explain_statement ( run_context& ctx, const sql::statement* item, query_sink* sink ) {

		static const char* const names [ 3 ] = { "id", "parent", "detail" };
		const sql::statement* inner = item->inner;
		prepared_select* plan = nullptr;

		if ( !item->query_plan )
			return fail_run ( ctx, "only EXPLAIN QUERY PLAN is supported" );

		switch ( inner->kind ) {

			case sql::STATEMENT_SELECT:
				plan = prepare_select ( ctx, inner->select, nullptr );
				break;

			case sql::STATEMENT_INSERT:
				if ( !inner->insert->query )
					return fail_run ( ctx, "this INSERT has no query to plan" );

				plan = prepare_select ( ctx, inner->insert->query, nullptr );
				break;

			case sql::STATEMENT_UPDATE:
				if ( !plan_target ( ctx, inner->update->table, inner->update->where, plan ) )
					return false;

				break;

			case sql::STATEMENT_DELETE:
				if ( !plan_target ( ctx, inner->remove->table, inner->remove->where, plan ) )
					return false;

				break;

			default:
				return fail_run ( ctx, "EXPLAIN QUERY PLAN works on SELECT, INSERT, UPDATE and DELETE" );

		}

		if ( !plan )
			return false;

		if ( !announce ( sink, names, 3 ) )
			return true;

		row_sink rows = { sink, forward_row };

		return explain_plan ( ctx, plan, rows );

	}

	static bool run_body ( run_context& ctx, const sql::statement* item, query_sink* sink ) {

		switch ( item->kind ) {

			case sql::STATEMENT_SELECT: {

				prepared_select* plan = prepare_select ( ctx, item->select, nullptr );

				if ( !plan )
					return false;

				if ( !announce ( sink, plan->names, plan->output_count ) )
					return true;

				row_sink rows = { sink, forward_row };

				return run_plan ( ctx, plan, nullptr, rows );

			}

			case sql::STATEMENT_INSERT:
				return run_insert ( ctx, item->insert );

			case sql::STATEMENT_UPDATE:
				return run_update ( ctx, item->update );

			case sql::STATEMENT_DELETE:
				return run_delete ( ctx, item->remove );

			case sql::STATEMENT_CREATE_TABLE:
				return run_create_table ( ctx, item->create_table );

			case sql::STATEMENT_CREATE_INDEX:
				return run_create_index ( ctx, item->create_index );

			case sql::STATEMENT_DROP_TABLE:
				return run_drop ( ctx, item->drop, false );

			case sql::STATEMENT_DROP_INDEX:
				return run_drop ( ctx, item->drop, true );

			case sql::STATEMENT_ALTER_TABLE:
				return run_alter ( ctx, item->alter );

			case sql::STATEMENT_EXPLAIN:
				return explain_statement ( ctx, item, sink );

			case sql::STATEMENT_ANALYZE:
				return run_analyze ( ctx );

			default:
				return fail_run ( ctx, "this statement cannot run here" );

		}

	}

	bool pager_problem ( database& item, const char* what ) {

		if ( !item.problem.size ) {

			item.problem.add ( what );

			if ( item.pages.problem [ 0 ] )
				item.problem.add ( ": " ).add ( item.pages.problem );

		}

		return false;

	}

	void start_context ( database& item, run_context& ctx, base::arena* lasting, base::arena* memory, base::arena* scratch, base::arena* results, const storage::value* parameters, uint32_t parameter_count ) {

		ctx.pages = &item.pages;
		ctx.schema = item.schema;
		ctx.lasting = lasting;
		ctx.memory = memory;
		ctx.scratch = scratch;
		ctx.results = results;
		ctx.parameters = parameters;
		ctx.parameter_count = parameter_count;
		ctx.problem = &item.problem;
		ctx.last_rowid = item.last_rowid;
		ctx.previous_changes = item.changes;
		ctx.total_changes = item.total_changes;
		ctx.random = &item.random;

	}

	bool run_statement ( database& item, run_context& ctx, const sql::statement* statement, query_sink* sink ) {

		bool handled = false;
		bool controlled = control_statement ( item, ctx, statement, handled );

		if ( handled )
			return controlled;

		switch ( statement->kind ) {

			case sql::STATEMENT_VACUUM:
				return run_vacuum ( item, ctx, statement );

			default:
				break;

		}

		if ( item.in_transaction && !storage::begin_statement ( item.pages ) )
			return pager_problem ( item, "the statement could not start" );

		bool fine = run_body ( ctx, statement, sink );

		if ( fine && !item.in_transaction && !storage::commit_pager ( item.pages ) ) {

			pager_problem ( item, "the change could not be saved" );
			fine = false;

		}

		if ( fine && item.in_transaction && !storage::end_statement ( item.pages ) ) {

			pager_problem ( item, "the statement could not finish" );
			fine = false;

		}

		if ( !fine ) {

			if ( item.in_transaction )
				storage::undo_statement ( item.pages );
			else
				storage::rollback_pager ( item.pages );

			if ( ctx.schema_changed || item.schema->version != item.pages.header.schema_version )
				reload_schema ( item );

			return false;

		}

		if ( statement->kind == sql::STATEMENT_INSERT || statement->kind == sql::STATEMENT_UPDATE || statement->kind == sql::STATEMENT_DELETE ) {

			item.changes = ctx.changes;
			item.total_changes += ctx.changes;

		}

		item.last_rowid = ctx.last_rowid;

		return true;

	}

	static void locate ( const char* sql, const char* at, uint32_t line, uint32_t column, uint32_t& out_line, uint32_t& out_column ) {

		uint32_t base_line = 1;
		uint32_t base_column = 1;

		for ( const char* walk = sql; walk < at; walk++ ) {

			if ( *walk == '\n' ) {

				base_line++;
				base_column = 1;

			} else
				base_column++;

		}

		out_line = base_line + line - 1;
		out_column = line == 1 ? base_column + column - 1 : column;

	}

	bool execute ( database& item, const char* sql, const storage::value* parameters, uint32_t parameter_count, query_sink* sink ) {

		item.problem.clear ( );
		item.parse_failed = false;
		item.error_line = 0;
		item.error_column = 0;
		item.statements = 0;

		if ( !item.open ) {

			item.problem.add ( "the database is not open" );

			return false;

		}

		if ( static_cast < uint8_t > ( sql [ 0 ] ) == 0xEF && static_cast < uint8_t > ( sql [ 1 ] ) == 0xBB && static_cast < uint8_t > ( sql [ 2 ] ) == 0xBF )
			sql += 3;

		const char* at = sql;

		for ( ;; ) {

			base::arena parse;
			base::arena lasting;
			base::arena memory;
			base::arena scratch;
			sql::statement* statement = nullptr;
			sql::parse_error error;
			size_t used = 0;

			if ( !sql::parse_one ( parse, at, used, statement, error ) ) {

				item.parse_failed = true;
				locate ( sql, at, error.line, error.column, item.error_line, item.error_column );
				item.problem.add ( error.message.value ( ) );

				return false;

			}

			if ( !statement )
				return true;

			base::arena results;
			run_context ctx = { };

			start_context ( item, ctx, &lasting, &memory, &scratch, &results, parameters, parameter_count );

			bool fine = run_statement ( item, ctx, statement, sink );

			item.rows_examined += ctx.rows_examined;
			item.statements++;

			if ( !fine ) {

				locate ( sql, at, 1, 1, item.error_line, item.error_column );

				if ( !item.problem.size )
					item.problem.add ( "the statement failed" );

				return false;

			}

			at += used;

		}

	}

}
