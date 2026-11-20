#include "engine/database/prepared.hh"

#include "sql/parser/parser.hh"

namespace engine {

	static void start_problem ( database& item ) {

		item.problem.clear ( );
		item.parse_failed = false;
		item.error_line = 0;
		item.error_column = 0;
		item.statements = 0;

	}

	static bool parse_failure ( database& item, const sql::parse_error& error ) {

		item.parse_failed = true;
		item.error_line = error.line;
		item.error_column = error.column;
		item.problem.add ( error.message.value ( ) );

		return false;

	}

	static bool reparse ( prepared_statement& item ) {

		item.parse.reset ( );
		item.lasting.reset ( );
		item.statement = nullptr;
		item.scopes = nullptr;
		item.next_id = 0;
		item.planned = false;
		item.stale = false;

		sql::parse_error error;
		size_t used = 0;

		if ( !sql::parse_one ( item.parse, item.text, used, item.statement, error ) )
			return parse_failure ( *item.owner, error );

		if ( !item.statement ) {

			item.owner->problem.add ( "the statement is empty" );

			return false;

		}

		return true;

	}

	bool prepare_statement ( database& item, const char* sql, prepared_statement*& out, size_t* used ) {

		out = nullptr;
		start_problem ( item );

		if ( used )
			*used = 0;

		if ( !item.open ) {

			item.problem.add ( "the database is not open" );

			return false;

		}

		size_t skipped = 0;

		if ( static_cast < uint8_t > ( sql [ 0 ] ) == 0xEF && static_cast < uint8_t > ( sql [ 1 ] ) == 0xBB && static_cast < uint8_t > ( sql [ 2 ] ) == 0xBF )
			skipped = 3;

		base::arena probe;
		sql::statement* statement = nullptr;
		sql::parse_error error;
		size_t consumed = 0;

		if ( !sql::parse_one ( probe, sql + skipped, consumed, statement, error ) )
			return parse_failure ( item, error );

		if ( used )
			*used = skipped + consumed;

		if ( !statement )
			return true;

		prepared_statement* made = base::make < prepared_statement > ( 1 );

		if ( !made || !( made->text = static_cast < char* > ( base::allocate ( consumed + 1 ) ) ) ) {

			base::release ( made );
			item.problem.add ( "out of memory" );

			return false;

		}

		base::copy ( made->text, sql + skipped, consumed );
		made->text [ consumed ] = 0;
		made->text_size = consumed;
		made->owner = &item;

		if ( !reparse ( *made ) ) {

			finish_statement ( made );

			return false;

		}

		out = made;

		return true;

	}

	bool run_prepared ( prepared_statement& item, const storage::value* parameters, uint32_t parameter_count, query_sink* sink ) {

		database& owner = *item.owner;

		start_problem ( owner );

		if ( !owner.open ) {

			owner.problem.add ( "the database is not open" );

			return false;

		}

		if ( ( item.stale || ( item.planned && item.loads != owner.schema->loads ) ) && !reparse ( item ) )
			return false;

		item.memory.recycle ( );
		item.scratch.recycle ( );
		item.results.recycle ( );

		run_context ctx = { };

		start_context ( owner, ctx, &item.lasting, &item.memory, &item.scratch, &item.results, parameters, parameter_count );
		ctx.scopes = item.scopes;
		ctx.next_id = item.next_id;

		uint64_t loads = owner.schema->loads;
		bool fine = run_statement ( owner, ctx, item.statement, sink );

		owner.rows_examined += ctx.rows_examined;
		owner.statements++;
		item.scopes = ctx.scopes;
		item.next_id = ctx.next_id;
		item.loads = loads;
		item.planned = true;

		if ( !fine ) {

			item.stale = true;

			if ( !owner.problem.size )
				owner.problem.add ( "the statement failed" );

		}

		return fine;

	}

	bool describe_statement ( prepared_statement& item, const char* const*& names, uint32_t& count ) {

		static const char* const explained [ 3 ] = { "id", "parent", "detail" };
		database& owner = *item.owner;

		names = nullptr;
		count = 0;
		start_problem ( owner );

		if ( ( item.stale || ( item.planned && item.loads != owner.schema->loads ) ) && !reparse ( item ) )
			return false;

		if ( item.statement->kind == sql::STATEMENT_EXPLAIN ) {

			names = explained;
			count = 3;

			return true;

		}

		bool returning = has_returning ( item.statement );

		if ( item.statement->kind != sql::STATEMENT_SELECT && !returning )
			return true;

		run_context ctx = { };

		start_context ( owner, ctx, &item.lasting, &item.memory, &item.scratch, &item.results, nullptr, 0 );
		ctx.scopes = item.scopes;
		ctx.next_id = item.next_id;

		uint64_t loads = owner.schema->loads;
		prepared_select* plan = nullptr;

		if ( returning ) {

			const char* table = nullptr;
			const sql::returning_clause* clause = returning_of ( item.statement, table );
			returning_plan* back = plan_returning ( ctx, table, returning_alias ( item.statement ), *clause );

			plan = back ? back->scope : nullptr;

		} else
			plan = prepare_select ( ctx, item.statement->select, nullptr );

		item.scopes = ctx.scopes;
		item.next_id = ctx.next_id;
		item.loads = loads;
		item.planned = true;

		if ( !plan ) {

			item.stale = true;

			return false;

		}

		names = plan->names;
		count = plan->output_count;

		return true;

	}

	const prepared_select* statement_plan ( const prepared_statement& item ) {

		if ( !item.statement )
			return nullptr;

		if ( has_returning ( item.statement ) ) {

			const char* table = nullptr;
			const sql::returning_clause* clause = returning_of ( item.statement, table );

			return clause->prepared ? static_cast < const returning_plan* > ( clause->prepared )->scope : nullptr;

		}

		if ( item.statement->kind != sql::STATEMENT_SELECT || !item.statement->select->prepared )
			return nullptr;

		return static_cast < const prepared_select* > ( item.statement->select->prepared );

	}

	bool statement_returns_rows ( const prepared_statement& item ) {

		return item.statement && ( item.statement->kind == sql::STATEMENT_SELECT || item.statement->kind == sql::STATEMENT_EXPLAIN || has_returning ( item.statement ) );

	}

	uint32_t statement_parameters ( const prepared_statement& item ) {

		return item.statement ? item.statement->parameter_count : 0;

	}

	void finish_statement ( prepared_statement* item ) {

		if ( !item )
			return;

		item->parse.reset ( );
		item->lasting.reset ( );
		item->memory.reset ( );
		item->scratch.reset ( );
		item->results.reset ( );
		base::release ( item->text );
		base::release ( item );

	}

}
