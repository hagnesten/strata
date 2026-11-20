#include "engine/catalog/catalog.hh"
#include "engine/value/decimal.hh"
#include "engine/value/moment.hh"
#include "sql/print/print.hh"

namespace engine {

	void clear_catalog ( catalog& item ) {

		item.memory.reset ( );
		item.table_count = 0;
		item.index_count = 0;
		item.view_count = 0;
		item.sequence_count = 0;
		item.loads++;

	}

	bool search_name ( const catalog& item, const char* name, char* out, size_t room ) {

		if ( !item.search [ 0 ] || !name )
			return false;

		for ( const char* at = name; *at; at++ )
			if ( *at == '.' )
				return false;

		size_t used = 0;

		for ( const char* at = item.search; *at; at++ )
			if ( used + 1 < room )
				out [ used++ ] = *at;

		if ( used + 1 < room )
			out [ used++ ] = '.';

		for ( const char* at = name; *at; at++ ) {

			if ( used + 1 >= room )
				return false;

			out [ used++ ] = *at;

		}

		out [ used ] = 0;

		return true;

	}

	bool set_search ( catalog& item, const char* schema ) {

		size_t size = 0;

		while ( schema && schema [ size ] )
			size++;

		if ( size >= CATALOG_SEARCH_ROOM )
			return false;

		bool plain = !size || base::same_folded ( schema, "public" ) || base::same_folded ( schema, "main" );

		for ( size_t i = 0; i < size; i++ )
			item.search [ i ] = plain ? 0 : schema [ i ];

		item.search [ plain ? 0 : size ] = 0;

		return true;

	}

	void setting_value ( const catalog& item, const char* name, base::text& out ) {

		static const char* const known [ ] [ 2 ] = {

			{ "server_version", "17.0" },
			{ "server_version_num", "170000" },
			{ "transaction_isolation", "serializable" },
			{ "default_transaction_isolation", "serializable" },
			{ "max_identifier_length", "63" },
			{ "max_index_keys", "32" },
			{ "standard_conforming_strings", "on" },
			{ "client_encoding", "UTF8" },
			{ "server_encoding", "UTF8" },
			{ "timezone", "UTC" },
			{ "datestyle", "ISO, MDY" },
			{ "intervalstyle", "postgres" },
			{ "integer_datetimes", "on" },
			{ "is_superuser", "on" },
			{ "lc_collate", "C" },
			{ "lc_ctype", "C" },
			{ "block_size", "4096" },

		};

		if ( base::same_folded ( name, "search_path" ) ) {

			out.add ( item.search [ 0 ] ? item.search : "public" );

			return;

		}

		for ( const auto& pair : known ) {

			if ( base::same_folded ( pair [ 0 ], name ) ) {

				out.add ( pair [ 1 ] );

				return;

			}

		}

	}

	table_info* find_table ( catalog& item, const char* name ) {

		char qualified [ CATALOG_NAME_ROOM ];

		if ( search_name ( item, name, qualified, sizeof ( qualified ) ) )
			for ( uint32_t i = 0; i < item.table_count; i++ )
				if ( base::same_folded ( item.tables [ i ].name, qualified ) )
					return &item.tables [ i ];

		for ( uint32_t i = 0; i < item.table_count; i++ )
			if ( base::same_folded ( item.tables [ i ].name, name ) )
				return &item.tables [ i ];

		return nullptr;

	}

	index_info* find_index ( catalog& item, const char* name ) {

		for ( uint32_t i = 0; i < item.index_count; i++ )
			if ( base::same_folded ( item.indexes [ i ].name, name ) )
				return &item.indexes [ i ];

		return nullptr;

	}

	template < typename entry > static entry* find_named ( catalog& item, entry* list, uint32_t count, const char* name ) {

		char qualified [ CATALOG_NAME_ROOM ];

		if ( search_name ( item, name, qualified, sizeof ( qualified ) ) )
			for ( uint32_t i = 0; i < count; i++ )
				if ( base::same_folded ( list [ i ].name, qualified ) )
					return &list [ i ];

		for ( uint32_t i = 0; i < count; i++ )
			if ( base::same_folded ( list [ i ].name, name ) )
				return &list [ i ];

		return nullptr;

	}

	view_info* find_view ( catalog& item, const char* name ) {

		return find_named ( item, item.views, item.view_count, name );

	}

	sequence_info* find_sequence ( catalog& item, const char* name ) {

		return find_named ( item, item.sequences, item.sequence_count, name );

	}

	void sequence_limits ( const sql::sequence_statement& item, sequence_info& out ) {

		if ( item.has_increment )
			out.increment = item.increment;

		if ( !out.increment )
			out.increment = 1;

		if ( item.has_cycle )
			out.cycle = item.cycle;

		bool rising = out.increment > 0;

		out.minimum = item.has_minimum ? item.minimum : rising ? 1 : static_cast < int64_t > ( 0x8000000000000000ull );
		out.maximum = item.has_maximum ? item.maximum : rising ? 0x7FFFFFFFFFFFFFFFll : -1;

		if ( item.has_start )
			out.start = item.start;
		else if ( !out.start )
			out.start = rising ? out.minimum : out.maximum;

	}

	int32_t find_column ( const table_info& table, const char* name ) {

		for ( uint32_t i = 0; i < table.column_count; i++ )
			if ( base::same_folded ( table.columns [ i ].name, name ) )
				return static_cast < int32_t > ( i );

		return -1;

	}

	static bool column_list ( const table_info& table, const char* const* names, uint32_t count, uint32_t* out, base::text& problem ) {

		if ( count > CATALOG_KEY_COLUMNS ) {

			problem.add ( "a key can have at most 32 columns" );

			return false;

		}

		for ( uint32_t i = 0; i < count; i++ ) {

			int32_t column = find_column ( table, names [ i ] );

			if ( column < 0 ) {

				problem.add ( "table " ).add ( table.name ).add ( " has no column named " ).add ( names [ i ] );

				return false;

			}

			out [ i ] = static_cast < uint32_t > ( column );

		}

		return true;

	}

	bool describe_table ( base::arena& memory, sql::create_table_statement* definition, table_info& out, base::text& problem ) {

		out = { };
		out.name = definition->name;
		out.definition = definition;
		out.rowid_column = -1;
		out.column_count = definition->column_count;
		out.columns = memory.make < column_info > ( definition->column_count );

		if ( !out.columns ) {

			problem.add ( "out of memory" );

			return false;

		}

		uint32_t primary_columns = 0;

		for ( uint32_t i = 0; i < definition->column_count; i++ ) {

			const sql::column_definition& column = definition->columns [ i ];

			for ( uint32_t k = 0; k < i; k++ ) {

				if ( base::same_folded ( column.name, definition->columns [ k ].name ) ) {

					problem.add ( "the column name " ).add ( column.name ).add ( " is used twice" );

					return false;

				}

			}

			column_info& info = out.columns [ i ];

			info.name = column.name;
			info.type = column.type;
			info.affinity = column.affinity;
			info.not_null = column.not_null || column.primary;
			info.nocase = column.nocase;
			info.exact = decimal_type ( column.type, info.precision, info.scale );
			info.moment = moment_type ( column.type );
			info.binary_json = base::find_folded ( column.type, "jsonb" );
			info.fallback = column.fallback;
			info.check = column.check;
			info.generated = column.generated;

			if ( column.generated ) {

				if ( column.primary || column.fallback ) {

					problem.add ( "the generated column " ).add ( column.name ).add ( " cannot also be a PRIMARY KEY or have a DEFAULT" );

					return false;

				}

				out.generated_count++;

			}

			if ( column.primary ) {

				primary_columns++;
				out.primary [ 0 ] = i;
				out.primary_count = 1;

			}

			if ( column.primary && column.affinity == sql::AFFINITY_INTEGER && ( base::same_folded ( column.type, "integer" ) || column.identity ) && !column.descending ) {

				info.rowid = true;
				info.not_null = false;
				out.rowid_column = static_cast < int32_t > ( i );
				out.autoincrement = column.autoincrement || column.identity;

			} else if ( column.autoincrement ) {

				problem.add ( "AUTOINCREMENT only works on an INTEGER PRIMARY KEY" );

				return false;

			}

			if ( column.check && out.check_count < CATALOG_CHECKS )
				out.checks [ out.check_count++ ] = column.check;

			if ( column.references && out.foreign_count < CATALOG_FOREIGN ) {

				foreign_key& key = out.foreign [ out.foreign_count++ ];

				key = { };
				key.columns [ 0 ] = i;
				key.column_count = 1;
				key.parent = column.references;
				key.on_delete = column.on_delete;

				if ( column.references_column ) {

					key.parent_columns [ 0 ] = column.references_column;
					key.parent_count = 1;

				}

			}

		}

		for ( uint32_t i = 0; i < definition->constraint_count; i++ ) {

			const sql::table_constraint& constraint = definition->constraints [ i ];
			uint32_t columns [ CATALOG_KEY_COLUMNS ];

			if ( ( constraint.primary || constraint.unique || constraint.references ) && !column_list ( out, constraint.columns, constraint.column_count, columns, problem ) )
				return false;

			if ( constraint.primary ) {

				primary_columns++;
				base::copy ( out.primary, columns, sizeof ( uint32_t ) * constraint.column_count );
				out.primary_count = constraint.column_count;

				if ( constraint.column_count == 1 && out.columns [ columns [ 0 ] ].affinity == sql::AFFINITY_INTEGER && ( base::same_folded ( out.columns [ columns [ 0 ] ].type, "integer" ) || definition->columns [ columns [ 0 ] ].identity ) ) {

					out.rowid_column = static_cast < int32_t > ( columns [ 0 ] );
					out.columns [ columns [ 0 ] ].rowid = true;
					out.autoincrement = out.autoincrement || definition->columns [ columns [ 0 ] ].identity;

				} else {

					for ( uint32_t k = 0; k < constraint.column_count; k++ )
						out.columns [ columns [ k ] ].not_null = true;

				}

			}

			if ( constraint.check && out.check_count < CATALOG_CHECKS )
				out.checks [ out.check_count++ ] = constraint.check;

			if ( constraint.references ) {

				if ( out.foreign_count == CATALOG_FOREIGN ) {

					problem.add ( "a table can have at most 32 foreign keys" );

					return false;

				}

				foreign_key& key = out.foreign [ out.foreign_count++ ];

				key = { };
				base::copy ( key.columns, columns, sizeof ( uint32_t ) * constraint.column_count );
				key.column_count = constraint.column_count;
				key.parent = constraint.references;
				key.on_delete = constraint.on_delete;
				key.parent_count = base::min < uint32_t > ( constraint.references_count, CATALOG_KEY_COLUMNS );

				for ( uint32_t k = 0; k < key.parent_count; k++ )
					key.parent_columns [ k ] = constraint.references_columns [ k ];

				if ( key.parent_count && key.parent_count != key.column_count ) {

					problem.add ( "a foreign key needs as many parent columns as child columns" );

					return false;

				}

			}

		}

		if ( primary_columns > 1 ) {

			problem.add ( "a table can have only one PRIMARY KEY" );

			return false;

		}

		for ( uint32_t i = 0; i < definition->column_count; i++ ) {

			out.columns [ i ].identity = definition->columns [ i ].identity && !out.columns [ i ].rowid;

			if ( out.columns [ i ].identity )
				out.identity_count++;

		}

		return true;

	}

	static void index_columns ( catalog& item, index_info& index, const table_info& table, const sql::create_index_statement* definition, base::text& problem ) {

		index.column_count = 0;

		if ( !column_list ( table, definition->columns, definition->column_count, index.columns, problem ) )
			return;

		index.column_count = definition->column_count;

	}

	bool ensure_schema_tree ( storage::pager& pages ) {

		if ( pages.header.schema_root )
			return true;

		uint32_t root = 0;

		if ( !storage::create_btree ( pages, false, root ) )
			return false;

		pages.header.schema_root = root;

		return true;

	}

	bool write_schema_row ( storage::pager& pages, const char* kind, const char* name, const char* table, uint32_t root, const char* sql, int64_t& rowid ) {

		if ( !ensure_schema_tree ( pages ) )
			return false;

		storage::btree tree = { &pages, pages.header.schema_root, false };

		if ( !rowid ) {

			bool empty = true;
			int64_t last = 0;

			if ( !storage::table_last ( tree, last, empty ) )
				return false;

			rowid = empty ? 1 : last + 1;

		}

		storage::value values [ SCHEMA_COLUMNS ];
		base::text record;
		bool existed = false;

		values [ 0 ] = storage::text_value ( kind, base::length ( kind ) );
		values [ 1 ] = storage::text_value ( name, base::length ( name ) );
		values [ 2 ] = storage::text_value ( table, base::length ( table ) );
		values [ 3 ] = storage::integer_value ( root );
		values [ 4 ] = storage::text_value ( sql, base::length ( sql ) );
		storage::encode_record ( values, SCHEMA_COLUMNS, record );
		pages.header.schema_version++;

		return storage::table_insert ( tree, rowid, reinterpret_cast < const uint8_t* > ( record.value ( ) ), record.size, true, existed );

	}

	bool delete_schema_row ( storage::pager& pages, int64_t rowid ) {

		storage::btree tree = { &pages, pages.header.schema_root, false };
		bool found = false;

		pages.header.schema_version++;

		return storage::table_delete ( tree, rowid, found );

	}

	static const char* keep_value_text ( base::arena& memory, const storage::value& item ) {

		if ( item.kind != storage::VALUE_TEXT )
			return nullptr;

		return memory.keep ( reinterpret_cast < const char* > ( item.bytes ), item.size );

	}

	bool load_catalog ( storage::pager& pages, catalog& out, base::text& problem ) {

		clear_catalog ( out );
		out.version = pages.header.schema_version;

		if ( !pages.header.schema_root )
			return true;

		storage::btree tree = { &pages, pages.header.schema_root, false };
		storage::cursor at;
		base::text payload;

		if ( !storage::cursor_first ( at, tree ) ) {

			problem.add ( "the schema could not be read: " ).add ( pages.problem );

			return false;

		}

		while ( at.valid ) {

			int64_t rowid = 0;
			storage::value values [ SCHEMA_COLUMNS ];
			uint32_t count = 0;

			if ( !storage::cursor_rowid ( at, rowid ) || !storage::cursor_payload ( at, payload ) || !storage::decode_record ( reinterpret_cast < const uint8_t* > ( payload.value ( ) ), payload.size, values, SCHEMA_COLUMNS, count ) || count != SCHEMA_COLUMNS || values [ 3 ].kind != storage::VALUE_INTEGER ) {

				problem.add ( "a schema row is damaged" );

				return false;

			}

			const char* kind = keep_value_text ( out.memory, values [ 0 ] );
			const char* name = keep_value_text ( out.memory, values [ 1 ] );
			const char* table = keep_value_text ( out.memory, values [ 2 ] );
			const char* text = keep_value_text ( out.memory, values [ 4 ] );
			uint32_t root = static_cast < uint32_t > ( values [ 3 ].integer );

			if ( !kind || !name || !table || !text ) {

				problem.add ( "a schema row is damaged" );

				return false;

			}

			sql::statement* parsed = nullptr;
			sql::parse_error error;
			size_t used = 0;

			if ( !sql::parse_one ( out.memory, text, used, parsed, error ) || !parsed ) {

				problem.add ( "the stored definition of " ).add ( name ).add ( " does not parse: " ).add ( error.message.value ( ) );

				return false;

			}

			if ( base::same ( kind, "table" ) && parsed->kind == sql::STATEMENT_CREATE_TABLE ) {

				if ( out.table_count == CATALOG_TABLES ) {

					problem.add ( "too many tables" );

					return false;

				}

				table_info& info = out.tables [ out.table_count ];

				if ( !describe_table ( out.memory, parsed->create_table, info, problem ) )
					return false;

				info.root = root;
				info.schema_rowid = rowid;
				info.sql = text;
				out.table_count++;

			} else if ( base::same ( kind, "index" ) && parsed->kind == sql::STATEMENT_CREATE_INDEX ) {

				if ( out.index_count == CATALOG_INDEXES ) {

					problem.add ( "too many indexes" );

					return false;

				}

				index_info& info = out.indexes [ out.index_count++ ];

				info = { };
				info.name = name;
				info.table = table;
				info.root = root;
				info.schema_rowid = rowid;
				info.unique = parsed->create_index->unique;
				info.fulltext = parsed->create_index->fulltext;
				info.automatic = base::starts_with ( name, "strata_auto_" );
				info.sql = text;
				info.column_count = parsed->create_index->column_count;

			} else if ( base::same ( kind, "view" ) && parsed->kind == sql::STATEMENT_CREATE_VIEW ) {

				if ( out.view_count == CATALOG_VIEWS ) {

					problem.add ( "too many views" );

					return false;

				}

				view_info& info = out.views [ out.view_count++ ];

				info = { };
				info.name = name;
				info.sql = text;
				info.schema_rowid = rowid;

			} else if ( base::same ( kind, "sequence" ) && parsed->kind == sql::STATEMENT_CREATE_SEQUENCE ) {

				if ( out.sequence_count == CATALOG_SEQUENCES ) {

					problem.add ( "too many sequences" );

					return false;

				}

				sequence_info& info = out.sequences [ out.sequence_count++ ];

				info = { };
				info.name = name;
				info.sql = text;
				info.schema_rowid = rowid;
				sequence_limits ( *parsed->sequence, info );

			} else {

				problem.add ( "the schema holds an entry of an unknown kind" );

				return false;

			}

			if ( !storage::cursor_next ( at ) ) {

				problem.add ( "the schema could not be read: " ).add ( pages.problem );

				return false;

			}

		}

		for ( uint32_t i = 0; i < out.index_count; i++ ) {

			index_info& index = out.indexes [ i ];
			table_info* table = find_table ( out, index.table );
			sql::statement* parsed = nullptr;
			sql::parse_error error;
			size_t used = 0;

			if ( !table || !sql::parse_one ( out.memory, index.sql, used, parsed, error ) || !parsed ) {

				problem.add ( "index " ).add ( index.name ).add ( " belongs to a missing table" );

				return false;

			}

			index_columns ( out, index, *table, parsed->create_index, problem );

			if ( problem.size )
				return false;

			index.table_root = table->root;

		}

		load_statistics ( pages, out );

		return true;

	}

}
