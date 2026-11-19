#include "engine/schema/schema.hh"

namespace engine {

	static sql::create_table_statement* copy_definition ( run_context& ctx, const sql::create_table_statement* from, uint32_t extra ) {

		sql::create_table_statement* made = ctx.lasting->make < sql::create_table_statement > ( 1 );
		sql::column_definition* columns = ctx.lasting->make < sql::column_definition > ( from->column_count + extra + 1 );
		sql::table_constraint* constraints = ctx.lasting->make < sql::table_constraint > ( from->constraint_count + 1 );

		if ( !made || !columns || !constraints ) {

			fail_run ( ctx, "out of memory" );

			return nullptr;

		}

		*made = *from;
		base::copy ( columns, from->columns, sizeof ( sql::column_definition ) * from->column_count );

		if ( from->constraint_count )
			base::copy ( constraints, from->constraints, sizeof ( sql::table_constraint ) * from->constraint_count );

		made->columns = columns;
		made->constraints = constraints;

		return made;

	}

	static const char** copy_names ( run_context& ctx, const char* const* names, uint32_t count ) {

		const char** made = ctx.lasting->make < const char* > ( count + 1 );

		if ( made && count )
			base::copy ( made, names, sizeof ( const char* ) * count );

		return made;

	}

	static void rename_in_expression ( sql::expression* item, const char* table, const char* from, const char* to ) {

		if ( !item )
			return;

		if ( item->kind == sql::EXPRESSION_COLUMN && item->name && base::same_folded ( item->name, from ) && ( !item->table || base::same_folded ( item->table, table ) ) )
			item->name = to;

		rename_in_expression ( item->left, table, from, to );
		rename_in_expression ( item->right, table, from, to );
		rename_in_expression ( item->third, table, from, to );

		for ( uint32_t i = 0; i < item->count; i++ )
			rename_in_expression ( item->list [ i ], table, from, to );

	}

	static bool rewrite_indexes ( run_context& ctx, const char* old_table, const char* new_table, const char* old_column, const char* new_column ) {

		for ( uint32_t i = 0; i < ctx.schema->index_count; i++ ) {

			index_info& index = ctx.schema->indexes [ i ];

			if ( !base::same_folded ( index.table, old_table ) )
				continue;

			table_info* table = find_table ( *ctx.schema, old_table );
			const char* names [ CATALOG_KEY_COLUMNS ];

			for ( uint32_t k = 0; k < index.column_count; k++ ) {

				names [ k ] = table->columns [ index.columns [ k ] ].name;

				if ( old_column && base::same_folded ( names [ k ], old_column ) )
					names [ k ] = new_column;

			}

			base::text text;
			int64_t rowid = index.schema_rowid;

			index_sql ( text, index.unique, index.name, new_table, names, index.column_count );

			if ( !write_schema_row ( *ctx.pages, "index", index.name, new_table, index.root, text.value ( ), rowid ) )
				return fail_storage ( ctx, "the schema could not be written" );

		}

		return true;

	}

	static bool rewrite_references ( run_context& ctx, const char* old_table, const char* new_table, const char* old_column, const char* new_column ) {

		for ( uint32_t t = 0; t < ctx.schema->table_count; t++ ) {

			table_info& other = ctx.schema->tables [ t ];

			if ( base::same_folded ( other.name, old_table ) )
				continue;

			sql::create_table_statement* definition = copy_definition ( ctx, other.definition, 0 );
			bool touched = false;

			if ( !definition )
				return false;

			for ( uint32_t c = 0; c < definition->column_count; c++ ) {

				sql::column_definition& column = definition->columns [ c ];

				if ( !column.references || !base::same_folded ( column.references, old_table ) )
					continue;

				column.references = new_table;
				touched = true;

				if ( old_column && column.references_column && base::same_folded ( column.references_column, old_column ) )
					column.references_column = new_column;

			}

			for ( uint32_t c = 0; c < definition->constraint_count; c++ ) {

				sql::table_constraint& constraint = definition->constraints [ c ];

				if ( !constraint.references || !base::same_folded ( constraint.references, old_table ) )
					continue;

				constraint.references = new_table;
				touched = true;

				if ( old_column ) {

					constraint.references_columns = copy_names ( ctx, constraint.references_columns, constraint.references_count );

					for ( uint32_t k = 0; k < constraint.references_count; k++ )
						if ( base::same_folded ( constraint.references_columns [ k ], old_column ) )
							constraint.references_columns [ k ] = new_column;

				}

			}

			if ( !touched )
				continue;

			int64_t rowid = other.schema_rowid;

			if ( !write_definition ( ctx, definition, other.root, rowid ) )
				return false;

		}

		return true;

	}

	static bool add_column ( run_context& ctx, table_info* table, const sql::alter_statement* item ) {

		sql::column_definition& column = *item->column;
		int64_t rowid = table->schema_rowid;

		if ( find_column ( *table, column.name ) >= 0 )
			return fail_named ( ctx, "table ", table->name, " already has a column with that name" );

		if ( column.primary || column.unique )
			return fail_run ( ctx, "an added column cannot be PRIMARY KEY or UNIQUE" );

		if ( !plain_value ( column.fallback ) )
			return fail_run ( ctx, "the DEFAULT of an added column must be a plain value" );

		if ( column.generated && column.not_null )
			return fail_run ( ctx, "an added generated column cannot be NOT NULL" );

		if ( column.not_null ) {

			storage::value fallback = storage::null_value ( );

			if ( column.fallback && ( !bind_expression ( ctx, nullptr, column.fallback, BIND_CONSTANT ) || !evaluate ( ctx, nullptr, column.fallback, fallback ) ) )
				return false;

			if ( fallback.kind == storage::VALUE_NULL )
				return fail_run ( ctx, "an added NOT NULL column needs a DEFAULT that is not NULL" );

		}

		sql::create_table_statement* definition = copy_definition ( ctx, table->definition, 1 );

		if ( !definition )
			return false;

		definition->columns [ definition->column_count++ ] = column;

		table_info info;

		if ( !validate_definition ( ctx, definition, info ) || !write_definition ( ctx, definition, table->root, rowid ) )
			return false;

		return refresh_schema ( ctx );

	}

	static bool rename_table ( run_context& ctx, table_info* table, const sql::alter_statement* item ) {

		bool skip = false;
		int64_t rowid = table->schema_rowid;

		if ( base::same_folded ( item->new_name, table->name ) )
			return true;

		if ( !name_taken ( ctx, item->new_name, false, skip ) )
			return false;

		if ( reserved_name ( item->new_name ) )
			return fail_run ( ctx, "names starting with strata_ are kept for Strata itself" );

		sql::create_table_statement* definition = copy_definition ( ctx, table->definition, 0 );

		if ( !definition )
			return false;

		definition->name = item->new_name;

		for ( uint32_t c = 0; c < definition->column_count; c++ )
			if ( definition->columns [ c ].references && base::same_folded ( definition->columns [ c ].references, table->name ) )
				definition->columns [ c ].references = item->new_name;

		for ( uint32_t c = 0; c < definition->constraint_count; c++ )
			if ( definition->constraints [ c ].references && base::same_folded ( definition->constraints [ c ].references, table->name ) )
				definition->constraints [ c ].references = item->new_name;

		if ( !write_definition ( ctx, definition, table->root, rowid ) || !rewrite_indexes ( ctx, table->name, item->new_name, nullptr, nullptr ) || !rewrite_references ( ctx, table->name, item->new_name, nullptr, nullptr ) )
			return false;

		return refresh_schema ( ctx );

	}

	static bool rename_column ( run_context& ctx, table_info* table, const sql::alter_statement* item ) {

		int32_t column = find_column ( *table, item->old_name );
		int64_t rowid = table->schema_rowid;

		if ( column < 0 )
			return fail_named ( ctx, "table ", table->name, " has no column with that name" );

		if ( find_column ( *table, item->new_name ) >= 0 && !base::same_folded ( item->old_name, item->new_name ) )
			return fail_named ( ctx, "table ", table->name, " already has a column with the new name" );

		sql::create_table_statement* definition = copy_definition ( ctx, table->definition, 0 );

		if ( !definition )
			return false;

		const char* old_name = table->columns [ column ].name;

		definition->columns [ column ].name = item->new_name;

		for ( uint32_t c = 0; c < definition->column_count; c++ ) {

			rename_in_expression ( definition->columns [ c ].check, table->name, old_name, item->new_name );
			rename_in_expression ( definition->columns [ c ].generated, table->name, old_name, item->new_name );

			if ( definition->columns [ c ].references && base::same_folded ( definition->columns [ c ].references, table->name ) && definition->columns [ c ].references_column && base::same_folded ( definition->columns [ c ].references_column, old_name ) )
				definition->columns [ c ].references_column = item->new_name;

		}

		for ( uint32_t c = 0; c < definition->constraint_count; c++ ) {

			sql::table_constraint& constraint = definition->constraints [ c ];

			rename_in_expression ( constraint.check, table->name, old_name, item->new_name );
			constraint.columns = copy_names ( ctx, constraint.columns, constraint.column_count );

			for ( uint32_t k = 0; k < constraint.column_count; k++ )
				if ( base::same_folded ( constraint.columns [ k ], old_name ) )
					constraint.columns [ k ] = item->new_name;

			if ( constraint.references && base::same_folded ( constraint.references, table->name ) ) {

				constraint.references_columns = copy_names ( ctx, constraint.references_columns, constraint.references_count );

				for ( uint32_t k = 0; k < constraint.references_count; k++ )
					if ( base::same_folded ( constraint.references_columns [ k ], old_name ) )
						constraint.references_columns [ k ] = item->new_name;

			}

		}

		if ( !write_definition ( ctx, definition, table->root, rowid ) || !rewrite_indexes ( ctx, table->name, table->name, old_name, item->new_name ) || !rewrite_references ( ctx, table->name, table->name, old_name, item->new_name ) )
			return false;

		return refresh_schema ( ctx );

	}

	static bool column_in_use ( run_context& ctx, const table_info& table, uint32_t column ) {

		const sql::create_table_statement* definition = table.definition;
		const sql::column_definition& doomed = definition->columns [ column ];
		const char* name = table.columns [ column ].name;

		if ( table.column_count == 1 )
			return fail_named ( ctx, "the only column of table ", table.name, " cannot be dropped" );

		if ( doomed.primary || table.rowid_column == static_cast < int32_t > ( column ) )
			return fail_named ( ctx, "the PRIMARY KEY column ", name, " cannot be dropped" );

		for ( uint32_t k = 0; k < table.primary_count; k++ )
			if ( table.primary [ k ] == column )
				return fail_named ( ctx, "the PRIMARY KEY column ", name, " cannot be dropped" );

		if ( doomed.unique )
			return fail_named ( ctx, "the UNIQUE column ", name, " cannot be dropped" );

		if ( doomed.references )
			return fail_named ( ctx, "the column ", name, " has a foreign key and cannot be dropped" );

		for ( uint32_t i = 0; i < ctx.schema->index_count; i++ ) {

			const index_info& index = ctx.schema->indexes [ i ];

			if ( !base::same_folded ( index.table, table.name ) )
				continue;

			for ( uint32_t k = 0; k < index.column_count; k++ ) {

				if ( index.columns [ k ] != column )
					continue;

				base::text line;

				line.add ( "the column " ).add ( name ).add ( " is used by index " ).add ( index.name ).add ( ", drop the index first" );

				return fail_run ( ctx, line.value ( ) );

			}

		}

		for ( uint32_t c = 0; c < definition->constraint_count; c++ ) {

			const sql::table_constraint& constraint = definition->constraints [ c ];

			if ( mentions_column ( constraint.check, table.name, name ) )
				return fail_named ( ctx, "the column ", name, " is used by a CHECK constraint" );

			for ( uint32_t k = 0; k < constraint.column_count; k++ )
				if ( base::same_folded ( constraint.columns [ k ], name ) )
					return fail_named ( ctx, "the column ", name, " is used by a table constraint" );

		}

		for ( uint32_t c = 0; c < definition->column_count; c++ ) {

			if ( c == column )
				continue;

			if ( mentions_column ( definition->columns [ c ].check, table.name, name ) )
				return fail_named ( ctx, "the column ", name, " is used by a CHECK constraint" );

			if ( mentions_column ( definition->columns [ c ].generated, table.name, name ) )
				return fail_named ( ctx, "the column ", name, " is used by a generated column" );

		}

		for ( uint32_t t = 0; t < ctx.schema->table_count; t++ ) {

			const table_info& other = ctx.schema->tables [ t ];

			for ( uint32_t f = 0; f < other.foreign_count; f++ ) {

				const foreign_key& link = other.foreign [ f ];

				if ( !base::same_folded ( link.parent, table.name ) )
					continue;

				for ( uint32_t k = 0; k < link.parent_count; k++ )
					if ( base::same_folded ( link.parent_columns [ k ], name ) )
						return fail_named ( ctx, "the column ", name, " is referenced by a foreign key" );

			}

		}

		return true;

	}

	static bool rebuild_without ( run_context& ctx, const table_info& table, uint32_t column, uint32_t& root ) {

		if ( !storage::create_btree ( *ctx.pages, false, root ) )
			return fail_storage ( ctx, "a new table tree could not be made" );

		storage::btree old_tree = { ctx.pages, table.root, false };
		storage::btree new_tree = { ctx.pages, root, false };
		storage::cursor at;
		base::text record;
		storage::value* values = ctx.scratch->make < storage::value > ( RECORD_COLUMNS );

		if ( !values )
			return fail_run ( ctx, "out of memory" );

		if ( !storage::cursor_first ( at, old_tree ) )
			return fail_storage ( ctx, "the table could not be read" );

		while ( at.valid ) {

			int64_t rowid = 0;
			uint32_t count = 0;
			bool existed = false;

			if ( !storage::cursor_rowid ( at, rowid ) || !storage::cursor_payload ( at, ctx.row_bytes ) )
				return fail_storage ( ctx, "the table could not be read" );

			if ( !storage::decode_record ( reinterpret_cast < const uint8_t* > ( ctx.row_bytes.value ( ) ), ctx.row_bytes.size, values, RECORD_COLUMNS, count ) )
				return fail_named ( ctx, "a row of table ", table.name, " is damaged" );

			if ( column < count ) {

				for ( uint32_t c = column; c + 1 < count; c++ )
					values [ c ] = values [ c + 1 ];

				count--;

			}

			record.clear ( );
			storage::encode_record ( values, count, record );

			if ( !storage::table_insert ( new_tree, rowid, reinterpret_cast < const uint8_t* > ( record.value ( ) ), record.size, false, existed ) )
				return fail_storage ( ctx, "a row could not be rewritten" );

			if ( !storage::cursor_next ( at ) )
				return fail_storage ( ctx, "the table could not be read" );

		}

		return true;

	}

	static bool drop_column ( run_context& ctx, table_info* table, const sql::alter_statement* item ) {

		int32_t found = find_column ( *table, item->old_name );
		int64_t rowid = table->schema_rowid;

		if ( found < 0 )
			return fail_named ( ctx, "table ", table->name, " has no column with that name" );

		uint32_t column = static_cast < uint32_t > ( found );

		if ( !column_in_use ( ctx, *table, column ) )
			return false;

		sql::create_table_statement* definition = copy_definition ( ctx, table->definition, 0 );

		if ( !definition )
			return false;

		for ( uint32_t c = column; c + 1 < definition->column_count; c++ )
			definition->columns [ c ] = definition->columns [ c + 1 ];

		definition->column_count--;

		table_info info;
		uint32_t root = 0;

		if ( !validate_definition ( ctx, definition, info ) || !rebuild_without ( ctx, *table, column, root ) )
			return false;

		if ( !storage::drop_btree ( *ctx.pages, table->root ) )
			return fail_storage ( ctx, "the old table tree could not be freed" );

		if ( !write_definition ( ctx, definition, root, rowid ) )
			return false;

		return refresh_schema ( ctx );

	}

	bool run_alter ( run_context& ctx, const sql::alter_statement* item ) {

		table_info* table = find_table ( *ctx.schema, item->table );

		if ( !table )
			return fail_named ( ctx, "no such table: ", item->table, "" );

		switch ( item->kind ) {

			case sql::ALTER_ADD_COLUMN:
				return add_column ( ctx, table, item );

			case sql::ALTER_RENAME_TABLE:
				return rename_table ( ctx, table, item );

			case sql::ALTER_RENAME_COLUMN:
				return rename_column ( ctx, table, item );

			case sql::ALTER_DROP_COLUMN:
				return drop_column ( ctx, table, item );

		}

		return fail_run ( ctx, "this ALTER TABLE is not supported" );

	}

}