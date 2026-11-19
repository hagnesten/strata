#include "engine/schema/schema.hh"
#include "sql/print/print.hh"

namespace engine {

	bool reserved_name ( const char* name ) {

		const char* prefix = "strata_";

		for ( uint32_t i = 0; prefix [ i ]; i++ )
			if ( base::lower ( name [ i ] ) != prefix [ i ] )
				return false;

		return true;

	}

	bool name_taken ( run_context& ctx, const char* name, bool if_not_exists, bool& skip ) {

		skip = false;

		if ( !find_table ( *ctx.schema, name ) && !find_index ( *ctx.schema, name ) )
			return true;

		if ( if_not_exists ) {

			skip = true;

			return true;

		}

		return fail_named ( ctx, "the name ", name, " is already used by a table or an index" );

	}

	bool uses_query ( const sql::expression* item ) {

		if ( !item )
			return false;

		if ( item->query )
			return true;

		if ( uses_query ( item->left ) || uses_query ( item->right ) || uses_query ( item->third ) )
			return true;

		for ( uint32_t i = 0; i < item->count; i++ )
			if ( uses_query ( item->list [ i ] ) )
				return true;

		return false;

	}

	bool plain_value ( const sql::expression* item ) {

		if ( !item )
			return true;

		if ( item->kind == sql::EXPRESSION_LITERAL )
			return true;

		return item->kind == sql::EXPRESSION_UNARY && ( item->op == sql::OPERATOR_NEGATE || item->op == sql::OPERATOR_PLUS ) && item->left && item->left->kind == sql::EXPRESSION_LITERAL;

	}

	bool refresh_schema ( run_context& ctx ) {

		ctx.scopes = nullptr;
		ctx.schema_changed = true;

		if ( !load_catalog ( *ctx.pages, *ctx.schema, *ctx.problem ) )
			return false;

		return true;

	}

	bool mentions_column ( const sql::expression* item, const char* table, const char* column ) {

		if ( !item )
			return false;

		if ( item->kind == sql::EXPRESSION_COLUMN && item->name && base::same_folded ( item->name, column ) && ( !item->table || base::same_folded ( item->table, table ) ) )
			return true;

		if ( mentions_column ( item->left, table, column ) || mentions_column ( item->right, table, column ) || mentions_column ( item->third, table, column ) )
			return true;

		for ( uint32_t i = 0; i < item->count; i++ )
			if ( mentions_column ( item->list [ i ], table, column ) )
				return true;

		return false;

	}

	void index_sql ( base::text& out, bool unique, const char* name, const char* table, const char* const* columns, uint32_t count ) {

		out.clear ( );
		out.add ( unique ? "CREATE UNIQUE INDEX " : "CREATE INDEX " );
		sql::print_name ( out, name );
		out.add ( " ON " );
		sql::print_name ( out, table );
		out.add ( " (" );

		for ( uint32_t k = 0; k < count; k++ ) {

			if ( k )
				out.add ( ", " );

			sql::print_name ( out, columns [ k ] );

		}

		out.add ( ')' );

	}

	bool validate_definition ( run_context& ctx, sql::create_table_statement* definition, table_info& info ) {

		if ( !describe_table ( *ctx.lasting, definition, info, *ctx.problem ) )
			return false;

		if ( definition->column_count > RECORD_COLUMNS )
			return fail_run ( ctx, "a table can have at most 256 columns" );

		prepared_select* scope = prepare_table_scope ( ctx, &info, info.name, nullptr, nullptr );

		if ( !scope )
			return false;

		for ( uint32_t i = 0; i < definition->column_count; i++ ) {

			sql::expression* fallback = definition->columns [ i ].fallback;

			if ( fallback && ( uses_query ( fallback ) || !bind_expression ( ctx, nullptr, fallback, BIND_CONSTANT ) ) )
				return ctx.problem->size ? false : fail_run ( ctx, "a DEFAULT cannot use a subquery" );

		}

		for ( uint32_t i = 0; i < info.check_count; i++ ) {

			if ( uses_query ( info.checks [ i ] ) )
				return fail_run ( ctx, "a CHECK cannot use a subquery" );

			if ( !bind_expression ( ctx, scope, info.checks [ i ], BIND_PLAIN ) )
				return false;

		}

		return true;

	}

	bool write_definition ( run_context& ctx, const sql::create_table_statement* definition, uint32_t root, int64_t& rowid ) {

		base::text text;

		sql::print_create_table ( text, definition );

		if ( !write_schema_row ( *ctx.pages, "table", definition->name, definition->name, root, text.value ( ), rowid ) )
			return fail_storage ( ctx, "the schema could not be written" );

		return true;

	}

	static bool add_automatic_index ( run_context& ctx, const char* table, const char* const* columns, uint32_t count, uint32_t& number ) {

		base::text name;

		do {

			name.clear ( );
			name.add ( "strata_auto_" ).add ( table ).add ( '_' ).number ( ++number );

		} while ( find_index ( *ctx.schema, name.value ( ) ) || find_table ( *ctx.schema, name.value ( ) ) );

		uint32_t root = 0;
		base::text text;
		int64_t rowid = 0;

		if ( !storage::create_btree ( *ctx.pages, true, root ) )
			return fail_storage ( ctx, "an index could not be made" );

		index_sql ( text, true, name.value ( ), table, columns, count );

		if ( !write_schema_row ( *ctx.pages, "index", name.value ( ), table, root, text.value ( ), rowid ) )
			return fail_storage ( ctx, "the schema could not be written" );

		return true;

	}

	static sql::create_table_statement* definition_from_query ( run_context& ctx, const sql::create_table_statement* item, prepared_select* plan ) {

		sql::create_table_statement* made = ctx.lasting->make < sql::create_table_statement > ( 1 );
		sql::column_definition* columns = ctx.lasting->make < sql::column_definition > ( plan->output_count + 1 );

		if ( !made || !columns ) {

			fail_run ( ctx, "out of memory" );

			return nullptr;

		}

		made->name = item->name;
		made->columns = columns;
		made->column_count = plan->output_count;

		for ( uint32_t c = 0; c < plan->output_count; c++ ) {

			const char* name = plan->names [ c ];
			base::text unique;

			unique.add ( name );

			for ( uint32_t n = 1;; n++ ) {

				bool clash = false;

				for ( uint32_t k = 0; k < c && !clash; k++ )
					clash = base::same_folded ( columns [ k ].name, unique.value ( ) );

				if ( !clash )
					break;

				unique.clear ( );
				unique.add ( name ).add ( ':' ).number ( n );

			}

			sql::affinity_kind affinity = plan->values_only ? sql::AFFINITY_BLOB : expression_affinity ( plan->outputs [ c ] );
			const char* type = affinity == sql::AFFINITY_INTEGER ? "INT" : affinity == sql::AFFINITY_REAL ? "REAL" : affinity == sql::AFFINITY_TEXT ? "TEXT" : affinity == sql::AFFINITY_NUMERIC ? "NUM" : "";

			columns [ c ].name = ctx.lasting->keep ( unique.value ( ), unique.size );
			columns [ c ].type = type;
			columns [ c ].affinity = sql::affinity_of ( type );

		}

		return made;

	}

	bool run_create_table ( run_context& ctx, const sql::create_table_statement* item ) {

		bool skip = false;

		if ( !name_taken ( ctx, item->name, item->if_not_exists, skip ) )
			return false;

		if ( skip )
			return true;

		if ( reserved_name ( item->name ) )
			return fail_run ( ctx, "names starting with strata_ are kept for Strata itself" );

		sql::create_table_statement* definition = const_cast < sql::create_table_statement* > ( item );
		collected_rows rows = { };

		rows.memory = ctx.memory;

		if ( item->query ) {

			prepared_select* plan = prepare_select ( ctx, item->query, nullptr );

			if ( !plan || !collect_plan ( ctx, plan, nullptr, rows ) )
				return false;

			definition = definition_from_query ( ctx, item, plan );

			if ( !definition )
				return false;

		}

		table_info info;

		if ( !validate_definition ( ctx, definition, info ) )
			return false;

		uint32_t root = 0;
		int64_t rowid = 0;

		if ( !storage::create_btree ( *ctx.pages, false, root ) )
			return fail_storage ( ctx, "the table could not be made" );

		if ( !write_definition ( ctx, definition, root, rowid ) )
			return false;

		uint32_t number = 0;

		for ( uint32_t i = 0; i < definition->column_count; i++ ) {

			const sql::column_definition& column = definition->columns [ i ];
			bool keyed = ( column.primary && !info.columns [ i ].rowid ) || column.unique;

			if ( keyed && !add_automatic_index ( ctx, definition->name, &column.name, 1, number ) )
				return false;

		}

		for ( uint32_t i = 0; i < definition->constraint_count; i++ ) {

			const sql::table_constraint& constraint = definition->constraints [ i ];
			bool rowid_key = constraint.primary && constraint.column_count == 1 && info.rowid_column >= 0 && base::same_folded ( constraint.columns [ 0 ], info.columns [ info.rowid_column ].name );

			if ( ( constraint.primary || constraint.unique ) && !rowid_key && !add_automatic_index ( ctx, definition->name, constraint.columns, constraint.column_count, number ) )
				return false;

		}

		if ( !refresh_schema ( ctx ) )
			return false;

		if ( !item->query )
			return true;

		table_info* table = find_table ( *ctx.schema, definition->name );

		if ( !table )
			return fail_run ( ctx, "the new table vanished" );

		for ( uint32_t r = 0; r < rows.count; r++ ) {

			base::arena_mark mark = ctx.scratch->mark ( );
			storage::value* values = ctx.scratch->make < storage::value > ( table->column_count + 1 );
			bool written = false;

			if ( !values )
				return fail_run ( ctx, "out of memory" );

			for ( uint32_t c = 0; c < table->column_count; c++ )
				values [ c ] = apply_affinity ( *ctx.scratch, rows.values [ static_cast < size_t > ( r ) * rows.width + c ], table->columns [ c ].affinity );

			if ( !insert_values ( ctx, *table, values, nullptr, sql::CONFLICT_ABORT, nullptr, written ) )
				return false;

			ctx.scratch->rewind ( mark );

		}

		return true;

	}

	bool run_create_index ( run_context& ctx, const sql::create_index_statement* item ) {

		bool skip = false;

		if ( !name_taken ( ctx, item->name, item->if_not_exists, skip ) )
			return false;

		if ( skip )
			return true;

		if ( reserved_name ( item->name ) )
			return fail_run ( ctx, "names starting with strata_ are kept for Strata itself" );

		table_info* table = find_table ( *ctx.schema, item->table );

		if ( !table )
			return fail_named ( ctx, "no such table: ", item->table, "" );

		if ( item->column_count > CATALOG_KEY_COLUMNS )
			return fail_run ( ctx, "an index can have at most 32 columns" );

		index_info index = { };

		index.name = item->name;
		index.table = table->name;
		index.unique = item->unique;
		index.column_count = item->column_count;

		const char* names [ CATALOG_KEY_COLUMNS ];

		for ( uint32_t k = 0; k < item->column_count; k++ ) {

			int32_t column = find_column ( *table, item->columns [ k ] );

			if ( column < 0 )
				return fail_named ( ctx, "table ", table->name, " has no column with that name for the index" );

			index.columns [ k ] = static_cast < uint32_t > ( column );
			names [ k ] = table->columns [ column ].name;

		}

		if ( !storage::create_btree ( *ctx.pages, true, index.root ) )
			return fail_storage ( ctx, "the index could not be made" );

		storage::btree rows = { ctx.pages, table->root, false };
		storage::btree tree = { ctx.pages, index.root, true };
		storage::cursor at;
		base::text key;
		base::text prefix;

		if ( !storage::cursor_first ( at, rows ) )
			return fail_storage ( ctx, "the table could not be read" );

		while ( at.valid ) {

			base::arena_mark mark = ctx.scratch->mark ( );
			int64_t rowid = 0;

			if ( !storage::cursor_rowid ( at, rowid ) || !storage::cursor_payload ( at, ctx.row_bytes ) )
				return fail_storage ( ctx, "the table could not be read" );

			char* kept = ctx.scratch->keep ( ctx.row_bytes.value ( ), ctx.row_bytes.size );
			storage::value* values = ctx.scratch->make < storage::value > ( table->column_count + 1 );

			if ( !kept || !values )
				return fail_run ( ctx, "out of memory" );

			if ( !decode_row ( ctx, *table, reinterpret_cast < const uint8_t* > ( kept ), ctx.row_bytes.size, rowid, values ) )
				return false;

			if ( index.unique ) {

				bool nulls = false;

				for ( uint32_t k = 0; k < index.column_count; k++ )
					nulls = nulls || values [ index.columns [ k ] ].kind == storage::VALUE_NULL;

				if ( !nulls ) {

					storage::cursor probe;

					make_index_key ( ctx, index, values, rowid, false, prefix );

					if ( !storage::cursor_seek_key ( probe, tree, reinterpret_cast < const uint8_t* > ( prefix.value ( ) ), prefix.size, index.column_count ) )
						return fail_storage ( ctx, "the index could not be searched" );

					if ( probe.valid ) {

						if ( !storage::cursor_payload ( probe, ctx.row_bytes ) )
							return fail_storage ( ctx, "the index could not be read" );

						if ( storage::compare_records ( reinterpret_cast < const uint8_t* > ( ctx.row_bytes.value ( ) ), ctx.row_bytes.size, reinterpret_cast < const uint8_t* > ( prefix.value ( ) ), prefix.size, index.column_count ) == 0 ) {

							base::text message;

							message.add ( "UNIQUE constraint failed: table " ).add ( table->name ).add ( " already holds duplicates for the index " ).add ( index.name );

							return fail_run ( ctx, message.value ( ) );

						}

					}

				}

			}

			bool existed = false;

			make_index_key ( ctx, index, values, rowid, true, key );

			if ( !storage::index_insert ( tree, reinterpret_cast < const uint8_t* > ( key.value ( ) ), key.size, existed ) )
				return fail_storage ( ctx, "the index could not be written" );

			if ( !storage::cursor_next ( at ) )
				return fail_storage ( ctx, "the table could not be read" );

			ctx.scratch->rewind ( mark );

		}

		base::text text;
		int64_t rowid = 0;

		index_sql ( text, index.unique, index.name, table->name, names, index.column_count );

		if ( !write_schema_row ( *ctx.pages, "index", index.name, table->name, index.root, text.value ( ), rowid ) )
			return fail_storage ( ctx, "the schema could not be written" );

		return refresh_schema ( ctx );

	}

	bool run_drop ( run_context& ctx, const sql::drop_statement* item, bool index ) {

		if ( index ) {

			index_info* found = find_index ( *ctx.schema, item->name );

			if ( !found )
				return item->if_exists ? true : fail_named ( ctx, "no such index: ", item->name, "" );

			if ( found->automatic )
				return fail_named ( ctx, "the index ", found->name, " belongs to a UNIQUE or PRIMARY KEY constraint and cannot be dropped" );

			if ( !storage::drop_btree ( *ctx.pages, found->root ) || !delete_schema_row ( *ctx.pages, found->schema_rowid ) )
				return fail_storage ( ctx, "the index could not be dropped" );

			return refresh_schema ( ctx );

		}

		table_info* table = find_table ( *ctx.schema, item->name );

		if ( !table )
			return item->if_exists ? true : fail_named ( ctx, "no such table: ", item->name, "" );

		for ( uint32_t t = 0; t < ctx.schema->table_count; t++ ) {

			table_info& other = ctx.schema->tables [ t ];

			if ( &other == table )
				continue;

			for ( uint32_t f = 0; f < other.foreign_count; f++ ) {

				if ( base::same_folded ( other.foreign [ f ].parent, table->name ) ) {

					base::text message;

					message.add ( "table " ).add ( table->name ).add ( " cannot be dropped while " ).add ( other.name ).add ( " has a foreign key pointing at it" );

					return fail_run ( ctx, message.value ( ) );

				}

			}

		}

		for ( uint32_t i = 0; i < ctx.schema->index_count; i++ ) {

			index_info& found = ctx.schema->indexes [ i ];

			if ( !base::same_folded ( found.table, table->name ) )
				continue;

			if ( !storage::drop_btree ( *ctx.pages, found.root ) || !delete_schema_row ( *ctx.pages, found.schema_rowid ) )
				return fail_storage ( ctx, "an index of the table could not be dropped" );

		}

		if ( !storage::drop_btree ( *ctx.pages, table->root ) || !delete_schema_row ( *ctx.pages, table->schema_rowid ) )
			return fail_storage ( ctx, "the table could not be dropped" );

		return refresh_schema ( ctx );

	}

}
