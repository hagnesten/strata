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

			index_sql ( text, index.unique, index.fulltext, index.name, new_table, names, index.column_count );

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

		const sql::expression* constant = column.fallback;

		while ( constant && constant->kind == sql::EXPRESSION_CAST )
			constant = constant->left;

		if ( !plain_value ( constant ) )
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

		if ( info.identity_count && !ensure_sequence_table ( ctx ) )
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

	static bool convert_value ( run_context& ctx, const column_info& column, storage::value& item ) {

		storage::value out = storage::null_value ( );

		if ( !column_value ( ctx, column, item, out ) )
			return false;

		bool numeric = column.affinity == sql::AFFINITY_INTEGER || column.affinity == sql::AFFINITY_REAL || ( column.affinity == sql::AFFINITY_NUMERIC && column.exact );

		if ( ctx.schema->postgres && numeric && ( out.kind == storage::VALUE_TEXT || out.kind == storage::VALUE_BLOB ) ) {

			base::text message;

			message.add ( "invalid input syntax for type " ).add ( column.type ).add ( ": \"" ).add ( reinterpret_cast < const char* > ( out.bytes ), base::min < size_t > ( out.size, 64 ) ).add ( '"' );

			return fail_run ( ctx, message.value ( ) );

		}

		item = out;

		return true;

	}

	static bool rebuild_table ( run_context& ctx, table_info* table, sql::create_table_statement* definition, int32_t converted ) {

		size_t size = base::length ( table->sql );
		char* text = ctx.lasting->keep ( table->sql, size );
		sql::statement* parsed = nullptr;
		sql::parse_error error = { };
		size_t used = 0;
		table_info* old = ctx.lasting->make < table_info > ( 1 );
		table_info* info = ctx.lasting->make < table_info > ( 1 );

		if ( !text || !old || !info )
			return fail_run ( ctx, "out of memory" );

		if ( !sql::parse_one ( *ctx.lasting, text, used, parsed, error ) || !parsed || parsed->kind != sql::STATEMENT_CREATE_TABLE )
			return fail_named ( ctx, "the definition of ", table->name, " could not be read" );

		if ( !describe_table ( *ctx.lasting, parsed->create_table, *old, *ctx.problem ) )
			return false;

		old->root = table->root;
		old->schema_rowid = table->schema_rowid;
		old->sql = text;

		if ( !validate_definition ( ctx, definition, *info ) )
			return false;

		kept_index* indexes = ctx.lasting->make < kept_index > ( ctx.schema->index_count + 1 );
		uint32_t index_count = 0;

		if ( !indexes )
			return fail_run ( ctx, "out of memory" );

		for ( uint32_t i = 0; i < ctx.schema->index_count; i++ ) {

			const index_info& index = ctx.schema->indexes [ i ];

			if ( !owned_by ( index, *table ) )
				continue;

			kept_index& keep = indexes [ index_count++ ];

			keep.name = ctx.lasting->keep ( index.name, base::length ( index.name ) );
			keep.sql = ctx.lasting->keep ( index.sql, base::length ( index.sql ) );
			keep.root = index.root;
			keep.schema_rowid = index.schema_rowid;
			keep.automatic = index.automatic;

			if ( !keep.name || !keep.sql )
				return fail_run ( ctx, "out of memory" );

		}

		const char* name = old->name;
		uint32_t root = 0;
		int64_t rowid = table->schema_rowid;

		if ( !storage::create_btree ( *ctx.pages, false, root ) )
			return fail_storage ( ctx, "a new table tree could not be made" );

		if ( !write_definition ( ctx, definition, root, rowid ) )
			return false;

		for ( uint32_t i = 0; i < index_count; i++ ) {

			kept_index& keep = indexes [ i ];

			if ( !storage::drop_btree ( *ctx.pages, keep.root ) )
				return fail_storage ( ctx, "an old index could not be freed" );

			if ( keep.automatic ) {

				if ( !delete_schema_row ( *ctx.pages, keep.schema_rowid ) )
					return fail_storage ( ctx, "the schema could not be written" );

				continue;

			}

			uint32_t fresh_root = 0;
			int64_t index_rowid = keep.schema_rowid;

			if ( !storage::create_btree ( *ctx.pages, true, fresh_root ) || !write_schema_row ( *ctx.pages, "index", keep.name, name, fresh_root, keep.sql, index_rowid ) )
				return fail_storage ( ctx, "an index could not be remade" );

		}

		if ( !add_key_indexes ( ctx, definition, *info ) )
			return false;

		if ( ( info->autoincrement || info->identity_count ) && !ensure_sequence_table ( ctx ) )
			return false;

		if ( !refresh_schema ( ctx ) )
			return false;

		table_info* fresh = find_table ( *ctx.schema, name );

		if ( !fresh )
			return fail_run ( ctx, "the rebuilt table vanished" );

		storage::btree old_tree = { ctx.pages, old->root, false };
		storage::cursor at;

		if ( !storage::cursor_first ( at, old_tree ) )
			return fail_storage ( ctx, "the table could not be read" );

		ctx.skip_parents = true;

		bool fine = true;

		while ( fine && at.valid ) {

			base::arena_mark mark = ctx.scratch->mark ( );
			int64_t row = 0;
			const uint8_t* data = nullptr;
			size_t bytes = 0;
			storage::value* values = nullptr;
			bool written = false;

			if ( !storage::cursor_take ( at, *ctx.scratch, row, data, bytes ) ) {

				fine = fail_storage ( ctx, "the table could not be read" );

				break;

			}

			values = ctx.scratch->make < storage::value > ( base::max ( old->column_count, fresh->column_count ) + 1 );

			if ( !values ) {

				fine = fail_run ( ctx, "out of memory" );

				break;

			}

			fine = decode_row ( ctx, *old, data, bytes, row, values );

			if ( fine && converted >= 0 )
				fine = convert_value ( ctx, fresh->columns [ converted ], values [ converted ] );

			storage::value kept_rowid = storage::integer_value ( row );

			if ( fine && fresh->rowid_column >= 0 ) {

				storage::value& key = values [ fresh->rowid_column ];

				if ( key.kind == storage::VALUE_NULL ) {

					base::text message;

					message.add ( "NOT NULL constraint failed: column " ).add ( fresh->columns [ fresh->rowid_column ].name ).add ( " of " ).add ( name ).add ( " contains null values" );
					fine = fail_run ( ctx, message.value ( ) );

				}

			}

			if ( fine )
				fine = insert_values ( ctx, *fresh, values, fresh->rowid_column >= 0 ? nullptr : &kept_rowid, sql::CONFLICT_ABORT, nullptr, written );

			ctx.scratch->rewind ( mark );

		}

		ctx.skip_parents = false;

		if ( !fine )
			return false;

		if ( fresh->foreign_count ) {

			storage::btree rows = { ctx.pages, fresh->root, false };

			if ( !storage::cursor_first ( at, rows ) )
				return fail_storage ( ctx, "the table could not be read" );

			while ( at.valid ) {

				base::arena_mark mark = ctx.scratch->mark ( );
				int64_t row = 0;
				const uint8_t* data = nullptr;
				size_t bytes = 0;
				storage::value* values = ctx.scratch->make < storage::value > ( fresh->column_count + 1 );

				if ( !values )
					return fail_run ( ctx, "out of memory" );

				if ( !storage::cursor_take ( at, *ctx.scratch, row, data, bytes ) )
					return fail_storage ( ctx, "the table could not be read" );

				if ( !decode_row ( ctx, *fresh, data, bytes, row, values ) || !check_parents_of ( ctx, *fresh, values ) )
					return false;

				ctx.scratch->rewind ( mark );

			}

		}

		if ( old->rowid_column >= 0 && old->autoincrement && fresh->columns [ old->rowid_column ].identity ) {

			int64_t last = 0;
			int64_t high = 0;
			int64_t current = 0;
			bool empty = true;
			bool found = false;
			base::text key;

			if ( !storage::table_last ( old_tree, last, empty ) || !read_counter ( ctx, name, high, found ) )
				return fail_storage ( ctx, "the identity could not be carried over" );

			key.add ( "strata_identity:" ).add ( name ).add ( '.' ).add ( fresh->columns [ old->rowid_column ].name );

			if ( !read_counter ( ctx, key.value ( ), current, found ) )
				return false;

			int64_t top = base::max ( empty ? 0 : last, high );

			if ( top > current && !write_counter ( ctx, key.value ( ), top ) )
				return false;

		}

		if ( fresh->rowid_column >= 0 && fresh->autoincrement && old->columns [ fresh->rowid_column ].identity ) {

			int64_t carried = 0;
			bool found = false;
			base::text key;

			key.add ( "strata_identity:" ).add ( name ).add ( '.' ).add ( old->columns [ fresh->rowid_column ].name );

			if ( !read_counter ( ctx, key.value ( ), carried, found ) )
				return false;

			if ( found && ( !raise_sequence ( ctx, *fresh, carried ) || !remove_counter ( ctx, key.value ( ) ) ) )
				return false;

		}

		if ( !storage::drop_btree ( *ctx.pages, old->root ) )
			return fail_storage ( ctx, "the old table tree could not be freed" );

		return true;

	}

	static bool scan_rows ( run_context& ctx, table_info& table, bool ( *visit ) ( run_context& ctx, table_info& table, storage::value* values, void* state ), void* state ) {

		storage::btree tree = { ctx.pages, table.root, false };
		storage::cursor at;

		if ( !storage::cursor_first ( at, tree ) )
			return fail_storage ( ctx, "the table could not be read" );

		while ( at.valid ) {

			base::arena_mark mark = ctx.scratch->mark ( );
			int64_t row = 0;
			const uint8_t* data = nullptr;
			size_t bytes = 0;
			storage::value* values = ctx.scratch->make < storage::value > ( table.column_count + 1 );

			if ( !values )
				return fail_run ( ctx, "out of memory" );

			if ( !storage::cursor_take ( at, *ctx.scratch, row, data, bytes ) )
				return fail_storage ( ctx, "the table could not be read" );

			if ( !decode_row ( ctx, table, data, bytes, row, values ) || !visit ( ctx, table, values, state ) )
				return false;

			ctx.scratch->rewind ( mark );

		}

		return true;

	}

	static bool track_highest ( run_context& ctx, table_info& table, storage::value* values, void* state ) {

		highest_value& high = *static_cast < highest_value* > ( state );
		storage::value number = apply_affinity ( *ctx.scratch, values [ high.column ], sql::AFFINITY_INTEGER );

		if ( number.kind == storage::VALUE_INTEGER && number.integer > high.value )
			high.value = number.integer;

		return true;

	}

	static bool find_alter_column ( run_context& ctx, table_info* table, const char* name, int32_t& column ) {

		column = find_column ( *table, name );

		if ( column < 0 ) {

			base::text message;

			message.add ( "no such column: " ).add ( name ).add ( " in table " ).add ( table->name );

			return fail_run ( ctx, message.value ( ) );

		}

		return true;

	}

	static bool in_primary ( const table_info& table, int32_t column ) {

		for ( uint32_t k = 0; k < table.primary_count; k++ )
			if ( static_cast < int32_t > ( table.primary [ k ] ) == column )
				return true;

		return false;

	}

	static bool finish_definition ( run_context& ctx, table_info* table, sql::create_table_statement* definition ) {

		table_info* info = ctx.lasting->make < table_info > ( 1 );
		int64_t rowid = table->schema_rowid;

		if ( !info )
			return fail_run ( ctx, "out of memory" );

		if ( !validate_definition ( ctx, definition, *info ) || !write_definition ( ctx, definition, table->root, rowid ) )
			return false;

		return refresh_schema ( ctx );

	}

	static bool same_layout ( run_context& ctx, table_info* table, sql::create_table_statement* definition, bool& same ) {

		table_info* info = ctx.lasting->make < table_info > ( 1 );

		if ( !info )
			return fail_run ( ctx, "out of memory" );

		if ( !validate_definition ( ctx, definition, *info ) )
			return false;

		same = info->rowid_column == table->rowid_column;

		return true;

	}

	static bool null_free ( run_context& ctx, table_info& table, storage::value* values, void* state ) {

		int32_t column = *static_cast < int32_t* > ( state );

		if ( values [ column ].kind != storage::VALUE_NULL )
			return true;

		base::text message;

		message.add ( "NOT NULL constraint failed: column " ).add ( table.columns [ column ].name ).add ( " of " ).add ( table.name ).add ( " contains null values" );

		return fail_run ( ctx, message.value ( ) );

	}

	static bool set_default ( run_context& ctx, table_info* table, const sql::alter_statement* item, bool drop ) {

		int32_t column = 0;

		if ( !find_alter_column ( ctx, table, item->old_name, column ) )
			return false;

		if ( !drop && ( uses_query ( item->value ) || table->columns [ column ].generated ) )
			return fail_run ( ctx, "this DEFAULT cannot be used here" );

		sql::create_table_statement* definition = copy_definition ( ctx, table->definition, 0 );

		if ( !definition )
			return false;

		definition->columns [ column ].fallback = drop ? nullptr : item->value;
		definition->columns [ column ].fallback_text = drop ? nullptr : item->value_text;

		return finish_definition ( ctx, table, definition );

	}

	static bool set_not_null ( run_context& ctx, table_info* table, const sql::alter_statement* item, bool required ) {

		int32_t column = 0;

		if ( !find_alter_column ( ctx, table, item->old_name, column ) )
			return false;

		if ( !required && ( in_primary ( *table, column ) || table->rowid_column == column ) )
			return fail_named ( ctx, "column ", table->columns [ column ].name, " is in a primary key" );

		if ( required && table->rowid_column != column && !scan_rows ( ctx, *table, null_free, &column ) )
			return false;

		sql::create_table_statement* definition = copy_definition ( ctx, table->definition, 0 );

		if ( !definition )
			return false;

		definition->columns [ column ].not_null = required;

		return finish_definition ( ctx, table, definition );

	}

	static bool same_storage ( const column_info& a, const column_info& b ) {

		return a.affinity == b.affinity && a.exact == b.exact && a.moment == b.moment && a.binary_json == b.binary_json && ( !a.exact || ( a.precision == b.precision && a.scale == b.scale ) );

	}

	static bool plain_using ( const sql::expression* item, const char* column ) {

		if ( !item )
			return true;

		if ( item->kind == sql::EXPRESSION_CAST )
			return plain_using ( item->left, column );

		return item->kind == sql::EXPRESSION_COLUMN && item->name && base::same_folded ( item->name, column );

	}

	static bool change_type ( run_context& ctx, table_info* table, const sql::alter_statement* item ) {

		int32_t column = 0;

		if ( !find_alter_column ( ctx, table, item->old_name, column ) )
			return false;

		if ( !plain_using ( item->value, table->columns [ column ].name ) )
			return fail_run ( ctx, "ALTER COLUMN TYPE supports USING only as a cast of the same column" );

		if ( table->columns [ column ].generated )
			return fail_named ( ctx, "the generated column ", table->columns [ column ].name, " cannot change type" );

		sql::create_table_statement* definition = copy_definition ( ctx, table->definition, 0 );

		if ( !definition )
			return false;

		sql::column_definition& changed = definition->columns [ column ];

		changed.type = item->column->type;
		changed.affinity = item->column->affinity;

		if ( item->column->identity && !changed.identity )
			changed.identity = true;

		table_info* info = ctx.lasting->make < table_info > ( 1 );

		if ( !info )
			return fail_run ( ctx, "out of memory" );

		if ( !validate_definition ( ctx, definition, *info ) )
			return false;

		if ( info->rowid_column == table->rowid_column && same_storage ( info->columns [ column ], table->columns [ column ] ) ) {

			int64_t rowid = table->schema_rowid;

			if ( !write_definition ( ctx, definition, table->root, rowid ) )
				return false;

			return refresh_schema ( ctx );

		}

		return rebuild_table ( ctx, table, definition, column );

	}

	static void bare_table_name ( base::text& out, const char* table ) {

		out.add ( catalog_view_alias ( table ) );

	}

	static void table_constraint_name ( base::text& out, const char* table, const sql::table_constraint& constraint ) {

		out.clear ( );

		if ( constraint.name ) {

			out.add ( constraint.name );

			return;

		}

		bare_table_name ( out, table );

		if ( constraint.primary ) {

			out.add ( "_pkey" );

			return;

		}

		if ( constraint.check ) {

			out.add ( "_check" );

			return;

		}

		for ( uint32_t k = 0; k < constraint.column_count; k++ )
			out.add ( '_' ).add ( constraint.columns [ k ] );

		out.add ( constraint.unique ? "_key" : "_fkey" );

	}

	static void column_constraint_name ( base::text& out, const char* table, const char* column, const char* suffix ) {

		out.clear ( );
		bare_table_name ( out, table );

		if ( !base::same ( suffix, "_pkey" ) )
			out.add ( '_' ).add ( column );

		out.add ( suffix );

	}

	static bool constraint_exists ( const table_info& table, const char* name ) {

		base::text candidate;
		const sql::create_table_statement* definition = table.definition;

		for ( uint32_t c = 0; c < definition->constraint_count; c++ ) {

			table_constraint_name ( candidate, table.name, definition->constraints [ c ] );

			if ( base::same_folded ( candidate.value ( ), name ) )
				return true;

		}

		for ( uint32_t c = 0; c < definition->column_count; c++ ) {

			const sql::column_definition& column = definition->columns [ c ];
			const char* suffixes [ 4 ] = { column.primary ? "_pkey" : nullptr, column.unique ? "_key" : nullptr, column.references ? "_fkey" : nullptr, column.check ? "_check" : nullptr };

			for ( const char* suffix : suffixes ) {

				if ( !suffix )
					continue;

				column_constraint_name ( candidate, table.name, column.name, suffix );

				if ( base::same_folded ( candidate.value ( ), name ) )
					return true;

			}

		}

		return false;

	}

	static bool add_constraint ( run_context& ctx, table_info* table, const sql::alter_statement* item ) {

		sql::table_constraint constraint = *item->constraint;
		base::text name;

		table_constraint_name ( name, table->name, constraint );

		if ( constraint_exists ( *table, name.value ( ) ) ) {

			base::text message;

			message.add ( "the name " ).add ( name.value ( ) ).add ( " is already used by a constraint of " ).add ( table->name );

			return fail_run ( ctx, message.value ( ) );

		}

		if ( constraint.primary && table->primary_count )
			return fail_named ( ctx, "table ", table->name, " already has a PRIMARY KEY" );

		constraint.name = ctx.lasting->keep ( name.value ( ), name.size );

		if ( !constraint.name )
			return fail_run ( ctx, "out of memory" );

		for ( uint32_t k = 0; k < constraint.column_count; k++ ) {

			int32_t column = 0;

			if ( !find_alter_column ( ctx, table, constraint.columns [ k ], column ) )
				return false;

		}

		sql::create_table_statement* definition = copy_definition ( ctx, table->definition, 0 );

		if ( !definition )
			return false;

		definition->constraints [ definition->constraint_count++ ] = constraint;

		return rebuild_table ( ctx, table, definition, -1 );

	}

	static bool key_in_use ( run_context& ctx, table_info* table, const char* name, const char* const* columns, uint32_t count, bool primary ) {

		for ( uint32_t t = 0; t < ctx.schema->table_count; t++ ) {

			const table_info& other = ctx.schema->tables [ t ];

			for ( uint32_t f = 0; f < other.foreign_count; f++ ) {

				const foreign_key& key = other.foreign [ f ];

				if ( !base::same_folded ( key.parent, table->name ) )
					continue;

				bool matches = false;

				if ( !key.parent_count )
					matches = primary;
				else if ( key.parent_count == count ) {

					matches = true;

					for ( uint32_t k = 0; k < count && matches; k++ ) {

						bool present = false;

						for ( uint32_t c = 0; c < count && !present; c++ )
							present = base::same_folded ( key.parent_columns [ k ], columns [ c ] );

						matches = present;

					}

				}

				if ( !matches )
					continue;

				base::text message;

				message.add ( "cannot drop constraint " ).add ( name ).add ( " on table " ).add ( table->name ).add ( " because a foreign key of " ).add ( other.name ).add ( " depends on it" );

				return fail_run ( ctx, message.value ( ) );

			}

		}

		return true;

	}

	static bool drop_constraint ( run_context& ctx, table_info* table, const sql::alter_statement* item ) {

		sql::create_table_statement* definition = copy_definition ( ctx, table->definition, 0 );
		base::text candidate;
		bool found = false;
		bool keyed = false;

		if ( !definition )
			return false;

		for ( uint32_t c = 0; c < definition->constraint_count && !found; c++ ) {

			table_constraint_name ( candidate, table->name, definition->constraints [ c ] );

			if ( !base::same_folded ( candidate.value ( ), item->old_name ) )
				continue;

			found = true;
			keyed = definition->constraints [ c ].primary || definition->constraints [ c ].unique;

			if ( keyed && !key_in_use ( ctx, table, item->old_name, definition->constraints [ c ].columns, definition->constraints [ c ].column_count, definition->constraints [ c ].primary ) )
				return false;

			for ( uint32_t k = c; k + 1 < definition->constraint_count; k++ )
				definition->constraints [ k ] = definition->constraints [ k + 1 ];

			definition->constraint_count--;

		}

		for ( uint32_t c = 0; c < definition->column_count && !found; c++ ) {

			sql::column_definition& column = definition->columns [ c ];

			if ( column.primary ) {

				column_constraint_name ( candidate, table->name, column.name, "_pkey" );

				if ( base::same_folded ( candidate.value ( ), item->old_name ) ) {

					if ( !key_in_use ( ctx, table, item->old_name, &column.name, 1, true ) )
						return false;

					column.primary = false;
					column.autoincrement = false;
					found = keyed = true;

					break;

				}

			}

			if ( column.unique ) {

				column_constraint_name ( candidate, table->name, column.name, "_key" );

				if ( base::same_folded ( candidate.value ( ), item->old_name ) ) {

					if ( !key_in_use ( ctx, table, item->old_name, &column.name, 1, false ) )
						return false;

					column.unique = false;
					found = keyed = true;

					break;

				}

			}

			if ( column.references ) {

				column_constraint_name ( candidate, table->name, column.name, "_fkey" );

				if ( base::same_folded ( candidate.value ( ), item->old_name ) ) {

					column.references = nullptr;
					column.references_column = nullptr;
					found = true;

					break;

				}

			}

			if ( column.check ) {

				column_constraint_name ( candidate, table->name, column.name, "_check" );

				if ( base::same_folded ( candidate.value ( ), item->old_name ) ) {

					column.check = nullptr;
					found = true;

					break;

				}

			}

		}

		if ( !found ) {

			if ( item->if_exists )
				return true;

			base::text message;

			message.add ( "constraint \"" ).add ( item->old_name ).add ( "\" of relation \"" ).add ( table->name ).add ( "\" does not exist" );

			return fail_run ( ctx, message.value ( ) );

		}

		if ( keyed )
			return rebuild_table ( ctx, table, definition, -1 );

		return finish_definition ( ctx, table, definition );

	}

	static bool rename_constraint ( run_context& ctx, table_info* table, const sql::alter_statement* item ) {

		sql::create_table_statement* definition = copy_definition ( ctx, table->definition, 0 );
		base::text candidate;

		if ( !definition )
			return false;

		if ( constraint_exists ( *table, item->new_name ) )
			return fail_named ( ctx, "the name ", item->new_name, " is already used by a constraint" );

		for ( uint32_t c = 0; c < definition->constraint_count; c++ ) {

			table_constraint_name ( candidate, table->name, definition->constraints [ c ] );

			if ( !base::same_folded ( candidate.value ( ), item->old_name ) )
				continue;

			definition->constraints [ c ].name = item->new_name;

			return finish_definition ( ctx, table, definition );

		}

		base::text message;

		message.add ( "constraint \"" ).add ( item->old_name ).add ( "\" of relation \"" ).add ( table->name ).add ( "\" does not exist" );

		return fail_run ( ctx, message.value ( ) );

	}

	static bool change_identity ( run_context& ctx, table_info* table, const sql::alter_statement* item, bool add ) {

		int32_t column = 0;

		if ( !find_alter_column ( ctx, table, item->old_name, column ) )
			return false;

		sql::create_table_statement* definition = copy_definition ( ctx, table->definition, 0 );

		if ( !definition )
			return false;

		sql::column_definition& changed = definition->columns [ column ];

		if ( add && changed.affinity != sql::AFFINITY_INTEGER )
			return fail_named ( ctx, "identity column ", changed.name, " needs an integer type" );

		if ( add && changed.identity )
			return fail_named ( ctx, "column ", changed.name, " is already an identity column" );

		if ( !add && !changed.identity )
			return item->if_exists ? true : fail_named ( ctx, "column ", changed.name, " is not an identity column" );

		changed.identity = add;
		changed.not_null = changed.not_null || add;

		if ( add )
			changed.fallback = nullptr;

		bool same = false;

		if ( !same_layout ( ctx, table, definition, same ) )
			return false;

		if ( !same )
			return rebuild_table ( ctx, table, definition, -1 );

		if ( add && !ensure_sequence_table ( ctx ) )
			return false;

		const char* table_name = ctx.lasting->keep ( table->name, base::length ( table->name ) );
		const char* column_name = ctx.lasting->keep ( table->columns [ column ].name, base::length ( table->columns [ column ].name ) );
		bool rowid_identity = table->rowid_column == column;

		if ( !table_name || !column_name )
			return fail_run ( ctx, "out of memory" );

		if ( !finish_definition ( ctx, table, definition ) )
			return false;

		if ( !add || rowid_identity )
			return true;

		table_info* fresh = find_table ( *ctx.schema, table_name );

		if ( !fresh )
			return fail_run ( ctx, "the table vanished" );

		highest_value high = { column, 0 };

		if ( !scan_rows ( ctx, *fresh, track_highest, &high ) )
			return false;

		base::text key;

		key.add ( "strata_identity:" ).add ( table_name ).add ( '.' ).add ( column_name );

		return write_counter ( ctx, key.value ( ), high.value );

	}

	static bool rename_index ( run_context& ctx, const sql::alter_statement* item ) {

		index_info* index = find_index ( *ctx.schema, item->table );

		if ( !index )
			return item->table_if_exists ? true : fail_named ( ctx, "no such index: ", item->table, "" );

		if ( base::same_folded ( index->name, item->new_name ) )
			return true;

		bool skip = false;

		if ( !name_taken ( ctx, item->new_name, false, skip ) )
			return false;

		table_info* table = find_table ( *ctx.schema, index->table );

		if ( !table )
			return fail_named ( ctx, "index ", index->name, " belongs to a missing table" );

		const char* names [ CATALOG_KEY_COLUMNS ];
		base::text text;
		int64_t rowid = index->schema_rowid;

		for ( uint32_t k = 0; k < index->column_count; k++ )
			names [ k ] = table->columns [ index->columns [ k ] ].name;

		index_sql ( text, index->unique, index->fulltext, item->new_name, table->name, names, index->column_count );

		if ( !write_schema_row ( *ctx.pages, "index", item->new_name, table->name, index->root, text.value ( ), rowid ) )
			return fail_storage ( ctx, "the schema could not be written" );

		return refresh_schema ( ctx );

	}

	bool run_alter ( run_context& ctx, const sql::alter_statement* item ) {

		if ( item->kind == sql::ALTER_RENAME_INDEX )
			return rename_index ( ctx, item );

		table_info* table = find_table ( *ctx.schema, item->table );

		if ( !table )
			return item->table_if_exists ? true : fail_named ( ctx, "no such table: ", item->table, "" );

		switch ( item->kind ) {

			case sql::ALTER_ADD_COLUMN:
				return add_column ( ctx, table, item );

			case sql::ALTER_RENAME_TABLE:
				return rename_table ( ctx, table, item );

			case sql::ALTER_RENAME_COLUMN:
				return rename_column ( ctx, table, item );

			case sql::ALTER_DROP_COLUMN:
				if ( item->if_exists && find_column ( *table, item->old_name ) < 0 )
					return true;

				return drop_column ( ctx, table, item );

			case sql::ALTER_SET_DEFAULT:
				return set_default ( ctx, table, item, false );

			case sql::ALTER_DROP_DEFAULT:
				return set_default ( ctx, table, item, true );

			case sql::ALTER_SET_NOT_NULL:
				return set_not_null ( ctx, table, item, true );

			case sql::ALTER_DROP_NOT_NULL:
				return set_not_null ( ctx, table, item, false );

			case sql::ALTER_COLUMN_TYPE:
				return change_type ( ctx, table, item );

			case sql::ALTER_ADD_CONSTRAINT:
				return add_constraint ( ctx, table, item );

			case sql::ALTER_DROP_CONSTRAINT:
				return drop_constraint ( ctx, table, item );

			case sql::ALTER_RENAME_CONSTRAINT:
				return rename_constraint ( ctx, table, item );

			case sql::ALTER_ADD_IDENTITY:
				return change_identity ( ctx, table, item, true );

			case sql::ALTER_DROP_IDENTITY:
				return change_identity ( ctx, table, item, false );

			default:
				break;

		}

		return fail_run ( ctx, "this ALTER TABLE is not supported" );

	}

}