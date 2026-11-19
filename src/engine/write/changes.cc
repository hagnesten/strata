#include "engine/run/run.hh"
#include "sql/print/print.hh"

namespace engine {

	static bool is_rowid_name ( const char* name ) {

		return base::same_folded ( name, "rowid" ) || base::same_folded ( name, "oid" ) || base::same_folded ( name, "_rowid_" );

	}

	static void column_names ( base::text& out, const table_info& table, const uint32_t* columns, uint32_t count ) {

		for ( uint32_t k = 0; k < count; k++ ) {

			if ( k )
				out.add ( ", " );

			out.add ( table.name ).add ( '.' ).add ( table.columns [ columns [ k ] ].name );

		}

	}

	static bool check_row ( run_context& ctx, table_info& table, storage::value* values, int64_t rowid, sql::conflict_kind conflict, bool& skipped ) {

		skipped = false;

		if ( !compute_generated ( ctx, table, values, rowid, 0 ) )
			return false;

		for ( uint32_t i = 0; i < table.column_count; i++ ) {

			const column_info& column = table.columns [ i ];

			if ( static_cast < int32_t > ( i ) == table.rowid_column || !column.not_null || values [ i ].kind != storage::VALUE_NULL )
				continue;

			if ( conflict == sql::CONFLICT_REPLACE && column.fallback ) {

				if ( !fallback_value ( ctx, column, values [ i ] ) )
					return false;

				if ( values [ i ].kind != storage::VALUE_NULL )
					continue;

			}

			if ( conflict == sql::CONFLICT_IGNORE ) {

				skipped = true;

				return true;

			}

			base::text message;

			message.add ( "NOT NULL constraint failed: " ).add ( table.name ).add ( '.' ).add ( column.name );

			return fail_run ( ctx, message.value ( ) );

		}

		if ( !table.check_count )
			return true;

		table_scope* scope = scope_for ( ctx, table );

		if ( !scope )
			return false;

		source_state& source = scope->state.sources [ 0 ];

		source.values = values;
		source.rowid = rowid;
		source.null_row = false;

		for ( uint32_t i = 0; i < table.check_count; i++ ) {

			truth_kind truth = TRUTH_NULL;

			if ( !condition ( ctx, &scope->state, table.checks [ i ], truth ) )
				return false;

			if ( truth != TRUTH_FALSE )
				continue;

			if ( conflict == sql::CONFLICT_IGNORE ) {

				skipped = true;

				return true;

			}

			base::text message;

			message.add ( "CHECK constraint failed: " ).add ( table.name ).add ( " (" );
			sql::print_expression ( message, table.checks [ i ] );
			message.add ( ')' );

			return fail_run ( ctx, message.value ( ) );

		}

		return true;

	}

	static bool resolve_parent ( run_context& ctx, const foreign_key& key, parent_link& link ) {

		link = { };
		link.parent = find_table ( *ctx.schema, key.parent );

		if ( !link.parent )
			return fail_named ( ctx, "a foreign key points at the missing table ", key.parent, "" );

		table_info& parent = *link.parent;

		if ( !key.parent_count ) {

			if ( !parent.primary_count )
				return fail_named ( ctx, "a foreign key points at ", parent.name, ", which has no PRIMARY KEY, so name the parent columns" );

			base::copy ( link.columns, parent.primary, sizeof ( uint32_t ) * parent.primary_count );
			link.count = parent.primary_count;

		} else {

			for ( uint32_t k = 0; k < key.parent_count; k++ ) {

				int32_t column = find_column ( parent, key.parent_columns [ k ] );

				if ( column < 0 && is_rowid_name ( key.parent_columns [ k ] ) )
					column = parent.rowid_column;

				if ( column < 0 )
					return fail_named ( ctx, "a foreign key points at a column that ", parent.name, " does not have" );

				link.columns [ k ] = static_cast < uint32_t > ( column );

			}

			link.count = key.parent_count;

		}

		if ( link.count != key.column_count )
			return fail_named ( ctx, "a foreign key needs as many columns as the key of ", parent.name, " it points at" );

		if ( link.count == 1 && static_cast < int32_t > ( link.columns [ 0 ] ) == parent.rowid_column ) {

			link.by_rowid = true;

			return true;

		}

		for ( uint32_t i = 0; i < ctx.schema->index_count; i++ ) {

			index_info& index = ctx.schema->indexes [ i ];

			if ( !index.unique || index.column_count != link.count || !base::same_folded ( index.table, parent.name ) )
				continue;

			bool all = true;

			for ( uint32_t k = 0; k < link.count && all; k++ ) {

				bool present = false;

				for ( uint32_t c = 0; c < index.column_count && !present; c++ )
					present = index.columns [ c ] == link.columns [ k ];

				all = present;

			}

			if ( all ) {

				link.index = &index;

				return true;

			}

		}

		return fail_named ( ctx, "a foreign key points at columns of ", parent.name, " that are not PRIMARY KEY or UNIQUE" );

	}

	static bool parent_exists ( run_context& ctx, const parent_link& link, const foreign_key& key, const storage::value* values, bool& exists ) {

		exists = true;

		for ( uint32_t k = 0; k < key.column_count; k++ )
			if ( values [ key.columns [ k ] ].kind == storage::VALUE_NULL )
				return true;

		table_info& parent = *link.parent;

		if ( link.by_rowid ) {

			storage::value number = apply_affinity ( *ctx.scratch, values [ key.columns [ 0 ] ], sql::AFFINITY_INTEGER );

			if ( number.kind != storage::VALUE_INTEGER ) {

				exists = false;

				return true;

			}

			storage::btree tree = { ctx.pages, parent.root, false };

			if ( !storage::table_find ( tree, number.integer, ctx.row_bytes, exists ) )
				return fail_storage ( ctx, "a parent row could not be read" );

			return true;

		}

		storage::value* row = ctx.scratch->make < storage::value > ( parent.column_count + 1 );

		if ( !row )
			return fail_run ( ctx, "out of memory" );

		for ( uint32_t k = 0; k < link.count; k++ )
			row [ link.columns [ k ] ] = apply_affinity ( *ctx.scratch, values [ key.columns [ k ] ], parent.columns [ link.columns [ k ] ].affinity );

		base::text probe;
		storage::btree tree = { ctx.pages, link.index->root, true };
		storage::cursor at;

		make_index_key ( ctx, *link.index, row, 0, false, probe );

		if ( !storage::cursor_seek_key ( at, tree, reinterpret_cast < const uint8_t* > ( probe.value ( ) ), probe.size, link.index->column_count ) )
			return fail_storage ( ctx, "a parent index could not be searched" );

		exists = false;

		if ( at.valid ) {

			if ( !storage::cursor_payload ( at, ctx.row_bytes ) )
				return fail_storage ( ctx, "a parent index could not be read" );

			exists = storage::compare_records ( reinterpret_cast < const uint8_t* > ( ctx.row_bytes.value ( ) ), ctx.row_bytes.size, reinterpret_cast < const uint8_t* > ( probe.value ( ) ), probe.size, link.index->column_count ) == 0;

		}

		return true;

	}

	static bool check_parents ( run_context& ctx, table_info& table, const storage::value* values, const storage::value* old ) {

		for ( uint32_t f = 0; f < table.foreign_count; f++ ) {

			const foreign_key& key = table.foreign [ f ];

			if ( old ) {

				bool changed = false;

				for ( uint32_t k = 0; k < key.column_count && !changed; k++ )
					changed = !same_value ( values [ key.columns [ k ] ], old [ key.columns [ k ] ] ) || values [ key.columns [ k ] ].kind != old [ key.columns [ k ] ].kind;

				if ( !changed )
					continue;

			}

			parent_link link;
			bool exists = true;

			if ( !resolve_parent ( ctx, key, link ) || !parent_exists ( ctx, link, key, values, exists ) )
				return false;

			if ( !exists ) {

				base::text message;

				message.add ( "FOREIGN KEY constraint failed: " );
				column_names ( message, table, key.columns, key.column_count );
				message.add ( " points at a row of " ).add ( link.parent->name ).add ( " that does not exist" );

				return fail_run ( ctx, message.value ( ) );

			}

		}

		return true;

	}

	static bool find_children ( run_context& ctx, table_info& parent, const storage::value* values, int64_t rowid, child_hit*& hits, uint32_t& count, const storage::value* changed_from ) {

		uint32_t room = 0;

		hits = nullptr;
		count = 0;

		for ( uint32_t t = 0; t < ctx.schema->table_count; t++ ) {

			table_info& child = ctx.schema->tables [ t ];

			for ( uint32_t f = 0; f < child.foreign_count; f++ ) {

				const foreign_key& key = child.foreign [ f ];

				if ( !base::same_folded ( key.parent, parent.name ) )
					continue;

				parent_link link;

				if ( !resolve_parent ( ctx, key, link ) )
					return false;

				storage::value wanted [ CATALOG_KEY_COLUMNS ];
				bool usable = true;
				bool differs = !changed_from;

				for ( uint32_t k = 0; k < link.count; k++ ) {

					wanted [ k ] = values [ link.columns [ k ] ];
					usable = usable && wanted [ k ].kind != storage::VALUE_NULL;

					if ( changed_from && ( !same_value ( changed_from [ link.columns [ k ] ], wanted [ k ] ) || changed_from [ link.columns [ k ] ].kind != wanted [ k ].kind ) )
						differs = true;

				}

				if ( !usable || !differs )
					continue;

				index_info* lookup = nullptr;

				for ( uint32_t i = 0; i < ctx.schema->index_count && !lookup; i++ ) {

					index_info& index = ctx.schema->indexes [ i ];
					bool leading = base::same_folded ( index.table, child.name ) && index.column_count >= key.column_count;

					for ( uint32_t k = 0; k < key.column_count && leading; k++ )
						leading = index.columns [ k ] == key.columns [ k ];

					if ( leading )
						lookup = &index;

				}

				if ( lookup ) {

					storage::value probe [ RUN_KEY_ROOM ];
					base::text prefix;
					storage::btree index_tree = { ctx.pages, lookup->root, true };
					storage::cursor at;

					for ( uint32_t k = 0; k < key.column_count; k++ )
						probe [ k ] = apply_affinity ( *ctx.scratch, wanted [ k ], child.columns [ key.columns [ k ] ].affinity );

					storage::encode_record ( probe, key.column_count, prefix );

					if ( !storage::cursor_seek_key ( at, index_tree, reinterpret_cast < const uint8_t* > ( prefix.value ( ) ), prefix.size, key.column_count ) )
						return fail_storage ( ctx, "a child index could not be searched" );

					while ( at.valid ) {

						if ( !storage::cursor_payload ( at, ctx.row_bytes ) )
							return fail_storage ( ctx, "a child index could not be read" );

						const uint8_t* entry = reinterpret_cast < const uint8_t* > ( ctx.row_bytes.value ( ) );

						if ( storage::compare_records ( entry, ctx.row_bytes.size, reinterpret_cast < const uint8_t* > ( prefix.value ( ) ), prefix.size, key.column_count ) != 0 )
							break;

						storage::value fields [ RUN_KEY_ROOM ];
						uint32_t count_fields = 0;

						if ( !storage::decode_record ( entry, ctx.row_bytes.size, fields, RUN_KEY_ROOM, count_fields ) || !count_fields || fields [ count_fields - 1 ].kind != storage::VALUE_INTEGER )
							return fail_named ( ctx, "an entry of index ", lookup->name, " is damaged" );

						int64_t child_rowid = fields [ count_fields - 1 ].integer;

						if ( !( &child == &parent && child_rowid == rowid ) ) {

							child_hit found = { &child, &key, child_rowid };

							if ( !base::push ( *ctx.scratch, hits, count, room, found ) )
								return fail_run ( ctx, "out of memory" );

						}

						if ( !storage::cursor_next ( at ) )
							return fail_storage ( ctx, "a child index could not be read" );

					}

					continue;

				}

				storage::btree tree = { ctx.pages, child.root, false };
				storage::cursor at;

				if ( !storage::cursor_first ( at, tree ) )
					return fail_storage ( ctx, "a child table could not be read" );

				while ( at.valid ) {

					base::arena_mark mark = ctx.scratch->mark ( );
					int64_t child_rowid = 0;
					storage::value* row = nullptr;

					if ( !storage::cursor_rowid ( at, child_rowid ) || !storage::cursor_payload ( at, ctx.row_bytes ) )
						return fail_storage ( ctx, "a child table could not be read" );

					char* kept = ctx.scratch->keep ( ctx.row_bytes.value ( ), ctx.row_bytes.size );

					row = ctx.scratch->make < storage::value > ( child.column_count + 1 );

					if ( !kept || !row )
						return fail_run ( ctx, "out of memory" );

					if ( !decode_row ( ctx, child, reinterpret_cast < const uint8_t* > ( kept ), ctx.row_bytes.size, child_rowid, row ) )
						return false;

					bool hit = !( &child == &parent && child_rowid == rowid );

					for ( uint32_t k = 0; k < key.column_count && hit; k++ ) {

						storage::value a = row [ key.columns [ k ] ];
						storage::value b = wanted [ k ];

						comparison_affinity ( *ctx.scratch, child.columns [ key.columns [ k ] ].affinity, link.parent->columns [ link.columns [ k ] ].affinity, a, b );
						hit = compare_with ( sql::OPERATOR_EQUAL, a, b ) == TRUTH_TRUE;

					}

					ctx.scratch->rewind ( mark );

					if ( hit ) {

						child_hit found = { &child, &key, child_rowid };

						if ( !base::push ( *ctx.scratch, hits, count, room, found ) )
							return fail_run ( ctx, "out of memory" );

					}

					if ( !storage::cursor_next ( at ) )
						return fail_storage ( ctx, "a child table could not be read" );

				}

			}

		}

		return true;

	}

	static bool store_row ( run_context& ctx, table_info& table, int64_t rowid, const storage::value* values, bool replace ) {

		storage::value* stored = ctx.scratch->make < storage::value > ( table.column_count + 1 );

		if ( !stored )
			return fail_run ( ctx, "out of memory" );

		base::copy ( stored, values, sizeof ( storage::value ) * table.column_count );

		if ( table.rowid_column >= 0 )
			stored [ table.rowid_column ] = storage::null_value ( );

		uint32_t width = table.column_count;

		while ( width && stored [ width - 1 ].kind == storage::VALUE_NULL && !table.columns [ width - 1 ].fallback )
			width--;

		base::text record;
		storage::btree tree = { ctx.pages, table.root, false };
		bool existed = false;

		storage::encode_record ( stored, width, record );

		if ( record.size > 1000000000 )
			return fail_run ( ctx, "a row cannot be larger than 1 GB" );

		if ( !storage::table_insert ( tree, rowid, reinterpret_cast < const uint8_t* > ( record.value ( ) ), record.size, replace, existed ) )
			return fail_storage ( ctx, "a row could not be written" );

		return true;

	}

	static bool change_indexes ( run_context& ctx, table_info& table, const storage::value* values, int64_t rowid, bool adding ) {

		base::text key;

		for ( uint32_t i = 0; i < ctx.schema->index_count; i++ ) {

			index_info& index = ctx.schema->indexes [ i ];

			if ( !base::same_folded ( index.table, table.name ) )
				continue;

			storage::btree tree = { ctx.pages, index.root, true };
			bool flag = false;

			make_index_key ( ctx, index, values, rowid, true, key );

			if ( adding ) {

				if ( !storage::index_insert ( tree, reinterpret_cast < const uint8_t* > ( key.value ( ) ), key.size, flag ) )
					return fail_storage ( ctx, "an index could not be written" );

			} else {

				if ( !storage::index_delete ( tree, reinterpret_cast < const uint8_t* > ( key.value ( ) ), key.size, flag ) )
					return fail_storage ( ctx, "an index could not be written" );

				if ( !flag )
					return fail_named ( ctx, "index ", index.name, " lost an entry, run a check on the database" );

			}

		}

		return true;

	}

	static bool find_conflict ( run_context& ctx, const index_info& index, const storage::value* values, bool exclude, int64_t excluded, int64_t& other, bool& found ) {

		found = false;

		for ( uint32_t k = 0; k < index.column_count; k++ )
			if ( values [ index.columns [ k ] ].kind == storage::VALUE_NULL )
				return true;

		base::text probe;
		storage::btree tree = { ctx.pages, index.root, true };
		storage::cursor at;

		make_index_key ( ctx, index, values, 0, false, probe );

		if ( !storage::cursor_seek_key ( at, tree, reinterpret_cast < const uint8_t* > ( probe.value ( ) ), probe.size, index.column_count ) )
			return fail_storage ( ctx, "an index could not be searched" );

		while ( at.valid ) {

			if ( !storage::cursor_payload ( at, ctx.row_bytes ) )
				return fail_storage ( ctx, "an index could not be read" );

			const uint8_t* entry = reinterpret_cast < const uint8_t* > ( ctx.row_bytes.value ( ) );

			if ( storage::compare_records ( entry, ctx.row_bytes.size, reinterpret_cast < const uint8_t* > ( probe.value ( ) ), probe.size, index.column_count ) != 0 )
				return true;

			storage::value key [ RUN_KEY_ROOM ];
			uint32_t count = 0;

			if ( !storage::decode_record ( entry, ctx.row_bytes.size, key, RUN_KEY_ROOM, count ) || !count || key [ count - 1 ].kind != storage::VALUE_INTEGER )
				return fail_named ( ctx, "an entry of index ", index.name, " is damaged" );

			if ( !exclude || key [ count - 1 ].integer != excluded ) {

				other = key [ count - 1 ].integer;
				found = true;

				return true;

			}

			if ( !storage::cursor_next ( at ) )
				return fail_storage ( ctx, "an index could not be read" );

		}

		return true;

	}

	static bool delete_row ( run_context& ctx, table_info& table, int64_t rowid, const storage::value* values );

	static bool update_row ( run_context& ctx, table_info& table, int64_t old_rowid, const storage::value* old, storage::value* now, sql::conflict_kind conflict, bool& skipped );

	static bool unique_failure ( run_context& ctx, const table_info& table, const index_info* index ) {

		base::text message;

		message.add ( "UNIQUE constraint failed: " );

		if ( index )
			column_names ( message, table, index->columns, index->column_count );
		else if ( table.rowid_column >= 0 )
			message.add ( table.name ).add ( '.' ).add ( table.columns [ table.rowid_column ].name );
		else
			message.add ( table.name ).add ( ".rowid" );

		return fail_run ( ctx, message.value ( ) );

	}

	static bool replace_other ( run_context& ctx, table_info& table, int64_t other ) {

		storage::value* values = nullptr;
		bool found = false;

		if ( !read_row ( ctx, table, other, values, found ) )
			return false;

		return !found || delete_row ( ctx, table, other, values );

	}

	static bool delete_row ( run_context& ctx, table_info& table, int64_t rowid, const storage::value* values ) {

		if ( ++ctx.depth > RUN_DEPTH ) {

			ctx.depth--;

			return fail_run ( ctx, "foreign key actions nest too deeply" );

		}

		child_hit* hits = nullptr;
		uint32_t count = 0;
		bool fine = find_children ( ctx, table, values, rowid, hits, count, nullptr );

		for ( uint32_t i = 0; i < count && fine; i++ ) {

			if ( hits [ i ].key->on_delete != sql::FOREIGN_RESTRICT )
				continue;

			base::text message;

			message.add ( "FOREIGN KEY constraint failed: rows of " ).add ( hits [ i ].table->name ).add ( " still point at the row being deleted from " ).add ( table.name );
			fine = fail_run ( ctx, message.value ( ) );

		}

		storage::btree tree = { ctx.pages, table.root, false };
		bool found = false;

		fine = fine && change_indexes ( ctx, table, values, rowid, false );

		if ( fine && !storage::table_delete ( tree, rowid, found ) )
			fine = fail_storage ( ctx, "a row could not be deleted" );

		for ( uint32_t i = 0; i < count && fine; i++ ) {

			table_info& child = *hits [ i ].table;
			storage::value* row = nullptr;
			bool present = false;

			fine = read_row ( ctx, child, hits [ i ].rowid, row, present );

			if ( !fine || !present )
				continue;

			if ( hits [ i ].key->on_delete == sql::FOREIGN_CASCADE )
				fine = delete_row ( ctx, child, hits [ i ].rowid, row );
			else {

				storage::value* now = ctx.scratch->make < storage::value > ( child.column_count + 1 );
				bool skipped = false;

				if ( !now ) {

					fine = fail_run ( ctx, "out of memory" );

					break;

				}

				base::copy ( now, row, sizeof ( storage::value ) * child.column_count );

				for ( uint32_t k = 0; k < hits [ i ].key->column_count; k++ )
					now [ hits [ i ].key->columns [ k ] ] = storage::null_value ( );

				fine = update_row ( ctx, child, hits [ i ].rowid, row, now, sql::CONFLICT_ABORT, skipped );

			}

		}

		ctx.depth--;

		return fine;

	}

	static bool update_row ( run_context& ctx, table_info& table, int64_t old_rowid, const storage::value* old, storage::value* now, sql::conflict_kind conflict, bool& skipped ) {

		skipped = false;

		int64_t new_rowid = old_rowid;

		if ( table.rowid_column >= 0 ) {

			const storage::value& given = now [ table.rowid_column ];

			if ( given.kind != storage::VALUE_INTEGER )
				return fail_named ( ctx, "the INTEGER PRIMARY KEY of ", table.name, given.kind == storage::VALUE_NULL ? " cannot be NULL" : " must be a whole number" );

			new_rowid = given.integer;

		}

		if ( !check_row ( ctx, table, now, new_rowid, conflict, skipped ) || skipped )
			return !ctx.problem->size;

		if ( !check_parents ( ctx, table, now, old ) )
			return false;

		child_hit* hits = nullptr;
		uint32_t count = 0;

		if ( !find_children ( ctx, table, old, old_rowid, hits, count, now ) )
			return false;

		if ( count ) {

			base::text message;

			message.add ( "FOREIGN KEY constraint failed: rows of " ).add ( hits [ 0 ].table->name ).add ( " still point at the old key of " ).add ( table.name );

			return fail_run ( ctx, message.value ( ) );

		}

		if ( new_rowid != old_rowid ) {

			storage::btree tree = { ctx.pages, table.root, false };
			bool taken = false;

			if ( !storage::table_find ( tree, new_rowid, ctx.row_bytes, taken ) )
				return fail_storage ( ctx, "a row could not be read" );

			if ( taken ) {

				if ( conflict == sql::CONFLICT_IGNORE ) {

					skipped = true;

					return true;

				}

				if ( conflict != sql::CONFLICT_REPLACE )
					return unique_failure ( ctx, table, nullptr );

				if ( !replace_other ( ctx, table, new_rowid ) )
					return false;

			}

		}

		for ( uint32_t i = 0; i < ctx.schema->index_count; i++ ) {

			index_info& index = ctx.schema->indexes [ i ];

			if ( !index.unique || !base::same_folded ( index.table, table.name ) )
				continue;

			int64_t other = 0;
			bool found = false;

			if ( !find_conflict ( ctx, index, now, true, old_rowid, other, found ) )
				return false;

			if ( !found )
				continue;

			if ( conflict == sql::CONFLICT_IGNORE ) {

				skipped = true;

				return true;

			}

			if ( conflict != sql::CONFLICT_REPLACE )
				return unique_failure ( ctx, table, &index );

			if ( !replace_other ( ctx, table, other ) )
				return false;

			i = static_cast < uint32_t > ( -1 );

		}

		if ( !change_indexes ( ctx, table, old, old_rowid, false ) )
			return false;

		if ( new_rowid != old_rowid ) {

			storage::btree tree = { ctx.pages, table.root, false };
			bool found = false;

			if ( !storage::table_delete ( tree, old_rowid, found ) )
				return fail_storage ( ctx, "a row could not be moved" );

		}

		return store_row ( ctx, table, new_rowid, now, new_rowid == old_rowid ) && change_indexes ( ctx, table, now, new_rowid, true );

	}

	static bool run_upsert ( run_context& ctx, table_info& table, upsert_plan& upsert, int64_t other, const storage::value* excluded, bool& written ) {

		written = false;

		storage::value* old = nullptr;
		bool found = false;

		if ( !read_row ( ctx, table, other, old, found ) )
			return false;

		if ( !found )
			return fail_run ( ctx, "the conflicting row vanished" );

		query_state state = { };

		state.plan = upsert.scope;
		state.sources = ctx.scratch->make < source_state > ( 2 );

		if ( !state.sources )
			return fail_run ( ctx, "out of memory" );

		state.sources [ 0 ].plan = &upsert.scope->sources [ 0 ];
		state.sources [ 0 ].values = old;
		state.sources [ 0 ].rowid = other;
		state.sources [ 1 ].plan = &upsert.scope->sources [ 1 ];
		state.sources [ 1 ].values = const_cast < storage::value* > ( excluded );
		state.sources [ 1 ].rowid = other;

		if ( upsert.syntax->update_where ) {

			truth_kind truth = TRUTH_NULL;

			if ( !condition ( ctx, &state, upsert.syntax->update_where, truth ) )
				return false;

			if ( truth != TRUTH_TRUE )
				return true;

		}

		storage::value* now = ctx.scratch->make < storage::value > ( table.column_count + 1 );

		if ( !now )
			return fail_run ( ctx, "out of memory" );

		base::copy ( now, old, sizeof ( storage::value ) * table.column_count );

		for ( uint32_t i = 0; i < upsert.syntax->update_count; i++ ) {

			storage::value value;
			uint32_t column = static_cast < uint32_t > ( upsert.columns [ i ] );

			if ( !evaluate ( ctx, &state, upsert.syntax->updates [ i ].value, value ) )
				return false;

			now [ column ] = apply_affinity ( *ctx.scratch, value, table.columns [ column ].affinity );

		}

		bool skipped = false;

		if ( !update_row ( ctx, table, other, old, now, sql::CONFLICT_ABORT, skipped ) )
			return false;

		written = !skipped;

		return true;

	}

	bool insert_values ( run_context& ctx, table_info& table, storage::value* values, const storage::value* explicit_rowid, sql::conflict_kind conflict, upsert_plan* upsert, bool& written ) {

		written = false;

		int64_t rowid = 0;
		bool chosen = false;
		const storage::value* given = table.rowid_column >= 0 ? &values [ table.rowid_column ] : explicit_rowid;

		if ( given && given->kind != storage::VALUE_NULL ) {

			storage::value number = apply_affinity ( *ctx.scratch, *given, sql::AFFINITY_INTEGER );

			if ( number.kind != storage::VALUE_INTEGER )
				return fail_named ( ctx, "the row id of ", table.name, " must be a whole number" );

			rowid = number.integer;
			chosen = true;

		}

		if ( !chosen ) {

			storage::btree tree = { ctx.pages, table.root, false };
			int64_t last = 0;
			bool empty = true;

			if ( !storage::table_last ( tree, last, empty ) )
				return fail_storage ( ctx, "a table could not be read" );

			if ( !empty && last == 0x7FFFFFFFFFFFFFFFll )
				return fail_named ( ctx, "table ", table.name, " has used every row id" );

			rowid = empty ? 1 : last + 1;

		}

		if ( table.rowid_column >= 0 )
			values [ table.rowid_column ] = storage::integer_value ( rowid );

		bool skipped = false;
		sql::conflict_kind plain = upsert ? sql::CONFLICT_ABORT : conflict;

		if ( !check_row ( ctx, table, values, rowid, plain, skipped ) )
			return false;

		if ( skipped )
			return true;

		if ( !check_parents ( ctx, table, values, nullptr ) )
			return false;

		auto clash = [ & ] ( int32_t target, const index_info* index, int64_t other, bool& stop ) {

			stop = false;

			if ( upsert && ( upsert->any_target || upsert->target == target ) ) {

				stop = true;

				if ( upsert->syntax->conflict == sql::CONFLICT_NOTHING )
					return true;

				return run_upsert ( ctx, table, *upsert, other, values, written );

			}

			if ( plain == sql::CONFLICT_IGNORE ) {

				stop = true;

				return true;

			}

			if ( plain == sql::CONFLICT_REPLACE )
				return replace_other ( ctx, table, other );

			return unique_failure ( ctx, table, index );

		};

		if ( chosen ) {

			storage::btree tree = { ctx.pages, table.root, false };
			bool taken = false;

			if ( !storage::table_find ( tree, rowid, ctx.row_bytes, taken ) )
				return fail_storage ( ctx, "a row could not be read" );

			if ( taken ) {

				bool stop = false;

				if ( !clash ( -1, nullptr, rowid, stop ) )
					return false;

				if ( stop )
					return true;

			}

		}

		for ( uint32_t i = 0; i < ctx.schema->index_count; i++ ) {

			index_info& index = ctx.schema->indexes [ i ];

			if ( !index.unique || !base::same_folded ( index.table, table.name ) )
				continue;

			int64_t other = 0;
			bool found = false;

			if ( !find_conflict ( ctx, index, values, false, 0, other, found ) )
				return false;

			if ( !found )
				continue;

			bool stop = false;

			if ( !clash ( static_cast < int32_t > ( i ), &index, other, stop ) )
				return false;

			if ( stop )
				return true;

			i = static_cast < uint32_t > ( -1 );

		}

		if ( !store_row ( ctx, table, rowid, values, false ) || !change_indexes ( ctx, table, values, rowid, true ) )
			return false;

		ctx.last_rowid = rowid;
		written = true;

		return true;

	}

	static int32_t target_column ( const table_info& table, const char* name ) {

		int32_t column = find_column ( table, name );

		if ( column < 0 && is_rowid_name ( name ) )
			return table.rowid_column >= 0 ? table.rowid_column : -2;

		return column;

	}

	static bool prepare_upsert ( run_context& ctx, table_info& table, const sql::insert_statement* item, upsert_plan& out ) {

		out = { };
		out.syntax = item;
		out.any_target = !item->conflict_count;
		out.target = -3;

		if ( item->conflict_count ) {

			uint32_t columns [ CATALOG_KEY_COLUMNS ];

			if ( item->conflict_count > CATALOG_KEY_COLUMNS )
				return fail_run ( ctx, "ON CONFLICT names too many columns" );

			for ( uint32_t k = 0; k < item->conflict_count; k++ ) {

				int32_t column = target_column ( table, item->conflict_columns [ k ] );

				if ( column < 0 )
					return fail_named ( ctx, "ON CONFLICT names a column that ", table.name, " does not have" );

				columns [ k ] = static_cast < uint32_t > ( column );

			}

			if ( item->conflict_count == 1 && static_cast < int32_t > ( columns [ 0 ] ) == table.rowid_column )
				out.target = -1;

			for ( uint32_t i = 0; i < ctx.schema->index_count && out.target == -3; i++ ) {

				index_info& index = ctx.schema->indexes [ i ];

				if ( !index.unique || index.column_count != item->conflict_count || !base::same_folded ( index.table, table.name ) )
					continue;

				bool all = true;

				for ( uint32_t k = 0; k < item->conflict_count && all; k++ ) {

					bool present = false;

					for ( uint32_t c = 0; c < index.column_count && !present; c++ )
						present = index.columns [ c ] == columns [ k ];

					all = present;

				}

				if ( all )
					out.target = static_cast < int32_t > ( i );

			}

			if ( out.target == -3 )
				return fail_run ( ctx, "ON CONFLICT must name the columns of a PRIMARY KEY or UNIQUE constraint" );

		}

		if ( item->conflict != sql::CONFLICT_UPDATE )
			return true;

		out.scope = prepare_table_scope ( ctx, &table, table.name, &table, "excluded" );
		out.columns = ctx.lasting->make < int32_t > ( item->update_count + 1 );

		if ( !out.scope || !out.columns )
			return fail_run ( ctx, "out of memory" );

		for ( uint32_t i = 0; i < item->update_count; i++ ) {

			int32_t column = target_column ( table, item->updates [ i ].column );

			if ( column < 0 )
				return fail_named ( ctx, "table ", table.name, " has no such column to update" );

			if ( table.columns [ column ].generated )
				return fail_named ( ctx, "cannot UPDATE the generated column ", item->updates [ i ].column, "" );

			out.columns [ i ] = column;

			if ( !bind_expression ( ctx, out.scope, item->updates [ i ].value, BIND_PLAIN ) )
				return false;

		}

		return bind_expression ( ctx, out.scope, item->update_where, BIND_PLAIN );

	}

	bool run_insert ( run_context& ctx, const sql::insert_statement* item ) {

		table_info* table = find_table ( *ctx.schema, item->table );

		if ( !table )
			return fail_named ( ctx, "no such table: ", item->table, "" );

		uint32_t target_count = item->column_count ? item->column_count : table->column_count - table->generated_count;
		int32_t* targets = ctx.lasting->make < int32_t > ( target_count + 1 );

		if ( !targets )
			return fail_run ( ctx, "out of memory" );

		for ( uint32_t k = 0, next = 0; k < target_count; k++ ) {

			if ( !item->column_count ) {

				while ( table->columns [ next ].generated )
					next++;

				targets [ k ] = static_cast < int32_t > ( next++ );

				continue;

			}

			int32_t column = target_column ( *table, item->columns [ k ] );

			if ( column == -1 ) {

				base::text message;

				message.add ( "table " ).add ( table->name ).add ( " has no column named " ).add ( item->columns [ k ] );

				return fail_run ( ctx, message.value ( ) );

			}

			if ( column >= 0 && table->columns [ column ].generated )
				return fail_named ( ctx, "cannot INSERT into the generated column ", item->columns [ k ], "" );

			for ( uint32_t j = 0; j < k; j++ )
				if ( targets [ j ] == column )
					return fail_named ( ctx, "the column ", item->columns [ k ], " is named twice" );

			targets [ k ] = column;

		}

		collected_rows rows = { };
		uint32_t row_count = 1;

		rows.memory = ctx.memory;

		if ( item->query ) {

			prepared_select* plan = prepare_select ( ctx, item->query, nullptr );

			if ( !plan )
				return false;

			if ( plan->output_count != target_count ) {

				base::text message;

				message.add ( "table " ).add ( table->name ).add ( " takes " ).number ( target_count ).add ( " values here but " ).number ( plan->output_count ).add ( " were given" );

				return fail_run ( ctx, message.value ( ) );

			}

			if ( !collect_plan ( ctx, plan, nullptr, rows ) )
				return false;

			row_count = rows.count;

		} else
			target_count = 0;

		upsert_plan upsert;
		bool upserting = item->conflict == sql::CONFLICT_UPDATE || item->conflict == sql::CONFLICT_NOTHING;

		if ( upserting && !prepare_upsert ( ctx, *table, item, upsert ) )
			return false;

		bool* targeted = ctx.lasting->make < bool > ( table->column_count + 1 );

		if ( !targeted )
			return fail_run ( ctx, "out of memory" );

		for ( uint32_t k = 0; k < target_count; k++ )
			if ( targets [ k ] >= 0 )
				targeted [ targets [ k ] ] = true;

		for ( uint32_t r = 0; r < row_count; r++ ) {

			base::arena_mark mark = ctx.scratch->mark ( );
			storage::value* values = ctx.scratch->make < storage::value > ( table->column_count + 1 );
			storage::value explicit_rowid = storage::null_value ( );
			bool has_rowid = false;

			if ( !values )
				return fail_run ( ctx, "out of memory" );

			for ( uint32_t c = 0; c < table->column_count; c++ )
				if ( !targeted [ c ] && !fallback_value ( ctx, table->columns [ c ], values [ c ] ) )
					return false;

			for ( uint32_t k = 0; k < target_count; k++ ) {

				const storage::value& given = rows.values [ static_cast < size_t > ( r ) * rows.width + k ];

				if ( targets [ k ] >= 0 )
					values [ targets [ k ] ] = apply_affinity ( *ctx.scratch, given, table->columns [ targets [ k ] ].affinity );
				else {

					explicit_rowid = given;
					has_rowid = true;

				}

			}

			bool written = false;

			if ( !insert_values ( ctx, *table, values, has_rowid ? &explicit_rowid : nullptr, item->conflict, upserting ? &upsert : nullptr, written ) )
				return false;

			if ( written )
				ctx.changes++;

			ctx.scratch->rewind ( mark );

		}

		return true;

	}

	static bool matching_rowids ( run_context& ctx, prepared_select* plan, query_state& state, int64_t*& rowids, uint32_t& count ) {

		uint32_t room = 0;
		prepared_source& definition = plan->sources [ 0 ];

		rowids = nullptr;
		count = 0;

		source_state& source = state.sources [ 0 ];

		if ( !open_source ( ctx, &state, source ) )
			return false;

		base::arena_mark mark = ctx.scratch->mark ( );

		for ( ;; ) {

			bool found = false;
			bool fine = true;

			if ( !next_source ( ctx, &state, source, found ) )
				return false;

			if ( !found )
				break;

			if ( !passes ( ctx, &state, definition.filters, definition.filter_count, fine ) )
				return false;

			if ( fine && !base::push ( *ctx.memory, rowids, count, room, source.rowid ) )
				return fail_run ( ctx, "out of memory" );

			ctx.scratch->rewind ( mark );

		}

		return true;

	}

	static bool target_scope ( run_context& ctx, table_info& table, sql::expression* where, prepared_select*& plan, query_state& state ) {

		plan = prepare_table_scope ( ctx, &table, table.name, nullptr, nullptr );

		if ( !plan )
			return false;

		state = { };
		state.plan = plan;
		state.sources = ctx.memory->make < source_state > ( 1 );

		if ( !state.sources )
			return fail_run ( ctx, "out of memory" );

		state.sources [ 0 ].plan = &plan->sources [ 0 ];

		if ( where ) {

			prepared_source& definition = plan->sources [ 0 ];
			uint32_t room = 0;

			if ( !bind_expression ( ctx, plan, where, BIND_PLAIN ) || !split_terms ( ctx, where, definition.filters, definition.filter_count, room ) )
				return false;

		}

		choose_path ( ctx, plan, 0 );

		return true;

	}

	bool plan_target ( run_context& ctx, const char* name, sql::expression* where, prepared_select*& plan ) {

		table_info* table = find_table ( *ctx.schema, name );

		if ( !table )
			return fail_named ( ctx, "no such table: ", name, "" );

		query_state state;

		return target_scope ( ctx, *table, where, plan, state );

	}

	bool run_update ( run_context& ctx, const sql::update_statement* item ) {

		table_info* table = find_table ( *ctx.schema, item->table );

		if ( !table )
			return fail_named ( ctx, "no such table: ", item->table, "" );

		prepared_select* plan = nullptr;
		query_state state;

		if ( !target_scope ( ctx, *table, item->where, plan, state ) )
			return false;

		int32_t* columns = ctx.lasting->make < int32_t > ( item->assignment_count + 1 );

		if ( !columns )
			return fail_run ( ctx, "out of memory" );

		for ( uint32_t i = 0; i < item->assignment_count; i++ ) {

			int32_t column = target_column ( *table, item->assignments [ i ].column );

			if ( column < 0 ) {

				base::text message;

				message.add ( "table " ).add ( table->name ).add ( " has no column named " ).add ( item->assignments [ i ].column );

				return fail_run ( ctx, message.value ( ) );

			}

			if ( table->columns [ column ].generated )
				return fail_named ( ctx, "cannot UPDATE the generated column ", item->assignments [ i ].column, "" );

			columns [ i ] = column;

			if ( !bind_expression ( ctx, plan, item->assignments [ i ].value, BIND_PLAIN ) )
				return false;

		}

		int64_t* rowids = nullptr;
		uint32_t count = 0;

		if ( !matching_rowids ( ctx, plan, state, rowids, count ) )
			return false;

		source_state& source = state.sources [ 0 ];

		for ( uint32_t r = 0; r < count; r++ ) {

			base::arena_mark mark = ctx.scratch->mark ( );
			storage::value* old = nullptr;
			bool found = false;

			if ( !read_row ( ctx, *table, rowids [ r ], old, found ) )
				return false;

			if ( !found )
				continue;

			source.values = old;
			source.rowid = rowids [ r ];
			source.null_row = false;

			storage::value* now = ctx.scratch->make < storage::value > ( table->column_count + 1 );

			if ( !now )
				return fail_run ( ctx, "out of memory" );

			base::copy ( now, old, sizeof ( storage::value ) * table->column_count );

			for ( uint32_t i = 0; i < item->assignment_count; i++ ) {

				storage::value value;

				if ( !evaluate ( ctx, &state, item->assignments [ i ].value, value ) )
					return false;

				now [ columns [ i ] ] = apply_affinity ( *ctx.scratch, value, table->columns [ columns [ i ] ].affinity );

			}

			bool skipped = false;

			if ( !update_row ( ctx, *table, rowids [ r ], old, now, item->conflict, skipped ) )
				return false;

			if ( !skipped )
				ctx.changes++;

			ctx.scratch->rewind ( mark );

		}

		return true;

	}

	bool run_delete ( run_context& ctx, const sql::delete_statement* item ) {

		table_info* table = find_table ( *ctx.schema, item->table );

		if ( !table )
			return fail_named ( ctx, "no such table: ", item->table, "" );

		prepared_select* plan = nullptr;
		query_state state;
		int64_t* rowids = nullptr;
		uint32_t count = 0;

		if ( !target_scope ( ctx, *table, item->where, plan, state ) || !matching_rowids ( ctx, plan, state, rowids, count ) )
			return false;

		for ( uint32_t r = 0; r < count; r++ ) {

			base::arena_mark mark = ctx.scratch->mark ( );
			storage::value* old = nullptr;
			bool found = false;

			if ( !read_row ( ctx, *table, rowids [ r ], old, found ) )
				return false;

			if ( found ) {

				if ( !delete_row ( ctx, *table, rowids [ r ], old ) )
					return false;

				ctx.changes++;

			}

			ctx.scratch->rewind ( mark );

		}

		return true;

	}

}
