#include "engine/schema/schema.hh"
#include "engine/search/search.hh"
#include "sql/print/print.hh"

namespace engine {

	bool reserved_name ( const char* name ) {

		const char* prefix = "strata_";

		for ( const char* at = name; *at; at++ )
			if ( *at == '.' )
				name = at + 1;

		for ( uint32_t i = 0; prefix [ i ]; i++ )
			if ( base::lower ( name [ i ] ) != prefix [ i ] )
				return false;

		return true;

	}

	bool name_taken ( run_context& ctx, const char* name, bool if_not_exists, bool& skip ) {

		skip = false;

		if ( !find_table ( *ctx.schema, name ) && !find_index ( *ctx.schema, name ) && !find_view ( *ctx.schema, name ) && !find_sequence ( *ctx.schema, name ) )
			return true;

		if ( if_not_exists ) {

			skip = true;

			return true;

		}

		return fail_named ( ctx, "the name ", name, " is already used by a table, index, view or sequence" );

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

	void index_sql ( base::text& out, bool unique, bool fulltext, const char* name, const char* table, const char* const* columns, uint32_t count ) {

		out.clear ( );
		out.add ( unique ? "CREATE UNIQUE INDEX " : fulltext ? "CREATE FULLTEXT INDEX " : "CREATE INDEX " );
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

	static bool visit_generated ( const table_info& info, uint32_t column, uint8_t* state );

	static bool walk_generated ( const sql::expression* item, const table_info& info, uint8_t* state ) {

		if ( !item )
			return true;

		if ( item->kind == sql::EXPRESSION_COLUMN && item->column >= 0 && static_cast < uint32_t > ( item->column ) < info.column_count && info.columns [ item->column ].generated && !visit_generated ( info, static_cast < uint32_t > ( item->column ), state ) )
			return false;

		if ( !walk_generated ( item->left, info, state ) || !walk_generated ( item->right, info, state ) || !walk_generated ( item->third, info, state ) )
			return false;

		for ( uint32_t i = 0; i < item->count; i++ )
			if ( !walk_generated ( item->list [ i ], info, state ) )
				return false;

		return true;

	}

	static bool visit_generated ( const table_info& info, uint32_t column, uint8_t* state ) {

		if ( state [ column ] == 2 )
			return true;

		if ( state [ column ] == 1 )
			return false;

		state [ column ] = 1;

		if ( !walk_generated ( info.columns [ column ].generated, info, state ) )
			return false;

		state [ column ] = 2;

		return true;

	}

	static bool check_generated ( run_context& ctx, sql::create_table_statement* definition, table_info& info, prepared_select* scope ) {

		if ( !info.generated_count )
			return true;

		for ( uint32_t i = 0; i < definition->column_count; i++ ) {

			sql::expression* generated = definition->columns [ i ].generated;

			if ( !generated )
				continue;

			if ( uses_query ( generated ) )
				return fail_run ( ctx, "a generated column cannot use a subquery" );

			if ( !bind_expression ( ctx, scope, generated, BIND_PLAIN ) )
				return false;

		}

		uint8_t* state = ctx.scratch->make < uint8_t > ( info.column_count );

		if ( !state )
			return fail_run ( ctx, "out of memory" );

		for ( uint32_t i = 0; i < info.column_count; i++ )
			if ( info.columns [ i ].generated && !visit_generated ( info, i, state ) )
				return fail_named ( ctx, "the generated column ", info.columns [ i ].name, " depends on itself" );

		return true;

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

		return check_generated ( ctx, definition, info, scope );

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

		index_sql ( text, true, false, name.value ( ), table, columns, count );

		if ( !write_schema_row ( *ctx.pages, "index", name.value ( ), table, root, text.value ( ), rowid ) )
			return fail_storage ( ctx, "the schema could not be written" );

		return true;

	}

	bool add_key_indexes ( run_context& ctx, const sql::create_table_statement* definition, const table_info& info ) {

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

		char qualified [ CATALOG_NAME_ROOM ];

		if ( search_name ( *ctx.schema, item->name, qualified, sizeof ( qualified ) ) ) {

			sql::create_table_statement* moved = ctx.lasting->make < sql::create_table_statement > ( 1 );
			size_t size = 0;

			while ( qualified [ size ] )
				size++;

			if ( !moved )
				return fail_run ( ctx, "out of memory" );

			*moved = *item;
			moved->name = ctx.lasting->keep ( qualified, size );
			item = moved;

		}

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

		if ( ( info.autoincrement || info.identity_count ) && !ensure_sequence_table ( ctx ) )
			return false;

		if ( !add_key_indexes ( ctx, definition, info ) || !refresh_schema ( ctx ) )
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
				if ( !column_value ( ctx, table->columns [ c ], rows.values [ static_cast < size_t > ( r ) * rows.width + c ], values [ c ] ) )
					return false;

			if ( !insert_values ( ctx, *table, values, nullptr, sql::CONFLICT_ABORT, nullptr, written ) )
				return false;

			ctx.scratch->rewind ( mark );

		}

		return true;

	}

	static bool fill_documents ( run_context& ctx, table_info& table, index_info& index ) {

		storage::btree rows = { ctx.pages, table.root, false };
		storage::cursor at;

		if ( !storage::cursor_first ( at, rows ) )
			return fail_storage ( ctx, "the table could not be read" );

		while ( at.valid ) {

			base::arena_mark mark = ctx.scratch->mark ( );
			int64_t rowid = 0;
			const uint8_t* data = nullptr;
			size_t size = 0;

			if ( !storage::cursor_take ( at, *ctx.scratch, rowid, data, size ) )
				return fail_storage ( ctx, "the table could not be read" );

			storage::value* values = ctx.scratch->make < storage::value > ( table.column_count + 1 );

			if ( !values )
				return fail_run ( ctx, "out of memory" );

			if ( !decode_row ( ctx, table, data, size, rowid, values ) || !index_document ( ctx, index, values, rowid, true ) )
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
		index.fulltext = item->fulltext;
		index.column_count = item->column_count;

		if ( index.fulltext && index.column_count > SEARCH_COLUMNS )
			return fail_run ( ctx, "a FULLTEXT index can have at most 63 columns" );

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

		storage::btree tree = { ctx.pages, index.root, true };

		if ( !( index.fulltext ? fill_documents ( ctx, *table, index ) : fill_index ( ctx, *table, index, tree ) ) )
			return false;

		base::text text;
		int64_t rowid = 0;

		index_sql ( text, index.unique, index.fulltext, index.name, table->name, names, index.column_count );

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

		if ( !forget_sequence ( ctx, *table ) )
			return false;

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

	static bool in_schema ( const char* table, const char* schema ) {

		size_t i = 0;

		for ( ; schema [ i ]; i++ )
			if ( base::lower ( table [ i ] ) != base::lower ( schema [ i ] ) )
				return false;

		return table [ i ] == '.';

	}

	static int32_t droppable_table ( catalog& schema, const char* name ) {

		for ( uint32_t t = 0; t < schema.table_count; t++ ) {

			table_info& table = schema.tables [ t ];

			if ( !in_schema ( table.name, name ) )
				continue;

			bool held = false;

			for ( uint32_t o = 0; o < schema.table_count && !held; o++ ) {

				if ( o == t )
					continue;

				for ( uint32_t f = 0; f < schema.tables [ o ].foreign_count && !held; f++ )
					held = base::same_folded ( schema.tables [ o ].foreign [ f ].parent, table.name );

			}

			if ( !held )
				return static_cast < int32_t > ( t );

		}

		return -1;

	}

	bool run_drop_schema ( run_context& ctx, const sql::drop_statement* item ) {

		bool any = false;

		for ( uint32_t t = 0; t < ctx.schema->table_count && !any; t++ )
			any = in_schema ( ctx.schema->tables [ t ].name, item->name );

		if ( !any )
			return true;

		if ( !item->cascade )
			return fail_named ( ctx, "cannot drop schema ", item->name, " because other objects depend on it, add CASCADE to drop its tables too" );

		for ( ;; ) {

			int32_t found = droppable_table ( *ctx.schema, item->name );

			if ( found < 0 )
				break;

			char name [ CATALOG_NAME_ROOM ];
			const char* from = ctx.schema->tables [ found ].name;
			size_t size = 0;

			while ( from [ size ] && size + 1 < sizeof ( name ) ) {

				name [ size ] = from [ size ];
				size++;

			}

			name [ size ] = 0;

			sql::drop_statement single = { };

			single.name = name;

			if ( !run_drop ( ctx, &single, false ) )
				return false;

		}

		for ( uint32_t t = 0; t < ctx.schema->table_count; t++ )
			if ( in_schema ( ctx.schema->tables [ t ].name, item->name ) )
				return fail_named ( ctx, "schema ", item->name, " has tables held by foreign keys from outside it" );

		return true;

	}

}
