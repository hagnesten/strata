#include "engine/schema/schema.hh"

#define VIEW_NAMESPACE_CATALOG 11
#define VIEW_NAMESPACE_PUBLIC 2200
#define VIEW_NAMESPACE_INFORMATION 13000
#define VIEW_NAMESPACE_FIRST 16000
#define VIEW_TABLE_FIRST 20000
#define VIEW_INDEX_FIRST 40000
#define VIEW_KEY_FIRST 60000
#define VIEW_ARRAY_RECEIVE 2400

namespace engine {

	activity_writer activity_hook = nullptr;

	terminate_handler terminate_hook = nullptr;

	struct view_type {

		uint32_t oid;
		const char* name;
		int32_t length;
		bool by_value;
		char kind;
		char category;
		uint32_t array;

	};

	struct view_rows {

		base::text out;
		uint32_t count;
		uint32_t width;

	};

	static const view_type view_types [ ] = {

		{ 16, "bool", 1, true, 'b', 'B', 1000 },
		{ 17, "bytea", -1, false, 'b', 'U', 1001 },
		{ 18, "char", 1, true, 'b', 'Z', 1002 },
		{ 19, "name", 64, false, 'b', 'S', 1003 },
		{ 20, "int8", 8, true, 'b', 'N', 1016 },
		{ 21, "int2", 2, true, 'b', 'N', 1005 },
		{ 22, "int2vector", -1, false, 'b', 'A', 1006 },
		{ 23, "int4", 4, true, 'b', 'N', 1007 },
		{ 24, "regproc", 4, true, 'b', 'N', 1008 },
		{ 25, "text", -1, false, 'b', 'S', 1009 },
		{ 26, "oid", 4, true, 'b', 'N', 1028 },
		{ 27, "tid", 6, false, 'b', 'U', 1010 },
		{ 28, "xid", 4, true, 'b', 'U', 1011 },
		{ 29, "cid", 4, true, 'b', 'U', 1012 },
		{ 30, "oidvector", -1, false, 'b', 'A', 1013 },
		{ 114, "json", -1, false, 'b', 'U', 199 },
		{ 142, "xml", -1, false, 'b', 'U', 143 },
		{ 600, "point", 16, false, 'b', 'G', 1017 },
		{ 650, "cidr", -1, false, 'b', 'I', 651 },
		{ 700, "float4", 4, true, 'b', 'N', 1021 },
		{ 701, "float8", 8, true, 'b', 'N', 1022 },
		{ 705, "unknown", -2, false, 'p', 'X', 0 },
		{ 790, "money", 8, true, 'b', 'N', 791 },
		{ 829, "macaddr", 6, false, 'b', 'U', 1040 },
		{ 869, "inet", -1, false, 'b', 'I', 1041 },
		{ 1042, "bpchar", -1, false, 'b', 'S', 1014 },
		{ 1043, "varchar", -1, false, 'b', 'S', 1015 },
		{ 1082, "date", 4, true, 'b', 'D', 1182 },
		{ 1083, "time", 8, true, 'b', 'D', 1183 },
		{ 1114, "timestamp", 8, true, 'b', 'D', 1115 },
		{ 1184, "timestamptz", 8, true, 'b', 'D', 1185 },
		{ 1186, "interval", 16, false, 'b', 'T', 1187 },
		{ 1266, "timetz", 12, false, 'b', 'D', 1270 },
		{ 1560, "bit", -1, false, 'b', 'V', 1561 },
		{ 1562, "varbit", -1, false, 'b', 'V', 1563 },
		{ 1700, "numeric", -1, false, 'b', 'N', 1231 },
		{ 2205, "regclass", 4, true, 'b', 'N', 2210 },
		{ 2206, "regtype", 4, true, 'b', 'N', 2211 },
		{ 2249, "record", -1, false, 'p', 'P', 2287 },
		{ 2278, "void", 4, true, 'p', 'P', 0 },
		{ 2950, "uuid", 16, false, 'b', 'U', 2951 },
		{ 3614, "tsvector", -1, false, 'b', 'U', 3643 },
		{ 3802, "jsonb", -1, false, 'b', 'U', 3807 },
		{ 4072, "jsonpath", -1, false, 'b', 'U', 4073 },

	};

	static const char* const view_settings [ ] = {

		"server_version", "server_version_num", "transaction_isolation", "default_transaction_isolation", "max_identifier_length",
		"max_index_keys", "standard_conforming_strings", "client_encoding", "server_encoding", "timezone", "datestyle",
		"intervalstyle", "integer_datetimes", "is_superuser", "lc_collate", "lc_ctype", "block_size", "search_path",

	};

	static bool type_has ( const char* type, const char* word ) {

		return base::find_folded ( type, word );

	}

	uint32_t postgres_type_of ( const char* type ) {

		if ( !type || !*type )
			return 0;

		if ( type_has ( type, "bool" ) )
			return 16;

		if ( base::same_folded ( type, "oid" ) || base::same_folded ( type, "regclass" ) || base::same_folded ( type, "regtype" ) )
			return 26;

		if ( type_has ( type, "bigint" ) || type_has ( type, "int8" ) || type_has ( type, "bigserial" ) )
			return 20;

		if ( type_has ( type, "smallint" ) || type_has ( type, "int2" ) )
			return 21;

		if ( type_has ( type, "interval" ) )
			return 1186;

		if ( type_has ( type, "point" ) )
			return 600;

		if ( type_has ( type, "int" ) || type_has ( type, "serial" ) )
			return 23;

		if ( type_has ( type, "timestamptz" ) || type_has ( type, "with time zone" ) )
			return type_has ( type, "timestamp" ) ? 1184 : 1266;

		if ( type_has ( type, "timestamp" ) || type_has ( type, "datetime" ) )
			return 1114;

		if ( type_has ( type, "date" ) )
			return 1082;

		if ( type_has ( type, "time" ) )
			return 1083;

		if ( type_has ( type, "uuid" ) )
			return 2950;

		if ( type_has ( type, "jsonb" ) )
			return 3802;

		if ( type_has ( type, "json" ) )
			return 114;

		if ( type_has ( type, "bytea" ) || type_has ( type, "blob" ) )
			return 17;

		if ( type_has ( type, "double" ) || type_has ( type, "float8" ) || type_has ( type, "float" ) )
			return 701;

		if ( type_has ( type, "real" ) || type_has ( type, "float4" ) )
			return 700;

		if ( type_has ( type, "numeric" ) || type_has ( type, "decimal" ) || type_has ( type, "money" ) )
			return 1700;

		if ( type_has ( type, "varchar" ) || type_has ( type, "varying" ) )
			return 1043;

		if ( type_has ( type, "char" ) )
			return 1042;

		if ( type_has ( type, "text" ) || type_has ( type, "clob" ) )
			return 25;

		return 0;

	}

	static uint32_t column_type_oid ( const column_info& column ) {

		uint32_t declared = postgres_type_of ( column.type );

		if ( declared )
			return declared;

		switch ( column.affinity ) {

			case sql::AFFINITY_INTEGER: return 20;
			case sql::AFFINITY_REAL: return 701;
			case sql::AFFINITY_NUMERIC: return 1700;
			case sql::AFFINITY_BLOB: return 17;
			default: return 25;

		}

	}

	static const char* type_name_of ( uint32_t oid ) {

		for ( const view_type& type : view_types )
			if ( type.oid == oid )
				return type.name;

		return "text";

	}

	static const char* standard_type_name ( uint32_t oid ) {

		switch ( oid ) {

			case 16: return "boolean";
			case 17: return "bytea";
			case 20: return "bigint";
			case 21: return "smallint";
			case 23: return "integer";
			case 25: return "text";
			case 114: return "json";
			case 700: return "real";
			case 701: return "double precision";
			case 1042: return "character";
			case 1043: return "character varying";
			case 1082: return "date";
			case 1083: return "time without time zone";
			case 1114: return "timestamp without time zone";
			case 1184: return "timestamp with time zone";
			case 1186: return "interval";
			case 1700: return "numeric";
			case 2950: return "uuid";
			case 3802: return "jsonb";
			default: return type_name_of ( oid );

		}

	}

	static void quoted ( base::text& out, const char* value ) {

		if ( !value ) {

			out.add ( "NULL" );

			return;

		}

		out.add ( '\'' );

		for ( const char* at = value; *at; at++ ) {

			if ( *at == '\'' )
				out.add ( '\'' );

			out.add ( *at );

		}

		out.add ( '\'' );

	}

	static void quoted_span ( base::text& out, const char* value, size_t size ) {

		out.add ( '\'' );

		for ( size_t i = 0; i < size; i++ ) {

			if ( value [ i ] == '\'' )
				out.add ( '\'' );

			out.add ( value [ i ] );

		}

		out.add ( '\'' );

	}

	static void start_row ( view_rows& rows ) {

		rows.out.add ( rows.count++ ? ", ( " : "( " );

	}

	static void end_row ( view_rows& rows ) {

		rows.out.add ( " )" );

	}

	static void finish_view ( base::text& sql, const char* select, view_rows& rows ) {

		sql.add ( "SELECT " ).add ( select ).add ( " FROM ( VALUES " );

		if ( rows.count )
			sql.add ( rows.out.value ( ), rows.out.size );
		else {

			sql.add ( "( " );

			for ( uint32_t i = 0; i < rows.width; i++ )
				sql.add ( i ? ", NULL" : "NULL" );

			sql.add ( " )" );

		}

		sql.add ( " ) AS v" );

		if ( !rows.count )
			sql.add ( " WHERE 0" );

	}

	static bool hidden_table ( const table_info& table ) {

		return reserved_name ( table.name );

	}

	static void split_name ( const char* name, const char*& schema, size_t& schema_size, const char*& bare ) {

		schema = nullptr;
		schema_size = 0;
		bare = name;

		for ( const char* at = name; *at; at++ ) {

			if ( *at == '.' ) {

				schema = name;
				schema_size = static_cast < size_t > ( at - name );
				bare = at + 1;

			}

		}

	}

	static uint32_t namespace_oid ( const catalog& schema, const char* name ) {

		const char* prefix = nullptr;
		size_t size = 0;
		const char* bare = nullptr;

		split_name ( name, prefix, size, bare );

		if ( !prefix )
			return VIEW_NAMESPACE_PUBLIC;

		uint32_t number = 0;

		for ( uint32_t t = 0; t < schema.table_count; t++ ) {

			const char* other = nullptr;
			size_t other_size = 0;
			const char* other_bare = nullptr;

			split_name ( schema.tables [ t ].name, other, other_size, other_bare );

			if ( !other )
				continue;

			bool seen = false;

			for ( uint32_t k = 0; k < t && !seen; k++ ) {

				const char* earlier = nullptr;
				size_t earlier_size = 0;
				const char* earlier_bare = nullptr;

				split_name ( schema.tables [ k ].name, earlier, earlier_size, earlier_bare );
				seen = earlier && earlier_size == other_size && base::same_folded_size ( earlier, other, other_size );

			}

			if ( seen )
				continue;

			if ( other_size == size && base::same_folded_size ( other, prefix, size ) )
				return VIEW_NAMESPACE_FIRST + number;

			number++;

		}

		return VIEW_NAMESPACE_PUBLIC;

	}

	static void schema_text ( base::text& out, const char* name ) {

		const char* prefix = nullptr;
		size_t size = 0;
		const char* bare = nullptr;

		split_name ( name, prefix, size, bare );

		if ( prefix )
			quoted_span ( out, prefix, size );
		else
			out.add ( "'public'" );

	}

	static void bare_text ( base::text& out, const char* name ) {

		const char* prefix = nullptr;
		size_t size = 0;
		const char* bare = nullptr;

		split_name ( name, prefix, size, bare );
		quoted ( out, bare );

	}

	static bool index_is_primary ( const table_info& table, const index_info& index ) {

		if ( !index.automatic || index.column_count != table.primary_count || !table.primary_count )
			return false;

		for ( uint32_t k = 0; k < index.column_count; k++ )
			if ( index.columns [ k ] != table.primary [ k ] )
				return false;

		return true;

	}

	static void build_namespace ( catalog& schema, base::text& sql ) {

		view_rows rows = { };

		rows.width = 2;
		start_row ( rows );
		rows.out.number ( VIEW_NAMESPACE_CATALOG ).add ( ", 'pg_catalog'" );
		end_row ( rows );
		start_row ( rows );
		rows.out.number ( VIEW_NAMESPACE_PUBLIC ).add ( ", 'public'" );
		end_row ( rows );
		start_row ( rows );
		rows.out.number ( VIEW_NAMESPACE_INFORMATION ).add ( ", 'information_schema'" );
		end_row ( rows );

		for ( uint32_t t = 0; t < schema.table_count; t++ ) {

			const char* prefix = nullptr;
			size_t size = 0;
			const char* bare = nullptr;

			split_name ( schema.tables [ t ].name, prefix, size, bare );

			if ( !prefix || namespace_oid ( schema, schema.tables [ t ].name ) < VIEW_NAMESPACE_FIRST )
				continue;

			bool seen = false;

			for ( uint32_t k = 0; k < t && !seen; k++ )
				seen = namespace_oid ( schema, schema.tables [ k ].name ) == namespace_oid ( schema, schema.tables [ t ].name );

			if ( seen )
				continue;

			start_row ( rows );
			rows.out.number ( namespace_oid ( schema, schema.tables [ t ].name ) ).add ( ", " );
			quoted_span ( rows.out, prefix, size );
			end_row ( rows );

		}

		finish_view ( sql, "CAST ( column1 AS oid ) AS oid, column2 AS nspname, CAST ( 10 AS oid ) AS nspowner, CAST ( NULL AS text ) AS nspacl", rows );

	}

	static void type_row ( view_rows& rows, uint32_t oid, const char* name, bool array_name, int32_t length, bool by_value, char kind, char category, uint32_t element, uint32_t array, uint32_t receive ) {

		start_row ( rows );
		rows.out.number ( oid ).add ( ", '" ).add ( array_name ? "_" : "" ).add ( name ).add ( "', " ).signed_number ( length ).add ( by_value ? ", 1, '" : ", 0, '" );
		rows.out.add ( kind ).add ( "', '" ).add ( category ).add ( "', " ).number ( element ).add ( ", " ).number ( array ).add ( ", " ).number ( receive );
		end_row ( rows );

	}

	static void build_type ( base::text& sql ) {

		view_rows rows = { };

		rows.width = 9;

		for ( const view_type& type : view_types ) {

			type_row ( rows, type.oid, type.name, false, type.length, type.by_value, type.kind, type.category, 0, type.array, 0 );

			if ( type.array )
				type_row ( rows, type.array, type.name, true, -1, false, 'b', 'A', type.oid, 0, VIEW_ARRAY_RECEIVE );

		}

		finish_view ( sql,
			"CAST ( column1 AS oid ) AS oid, column2 AS typname, CAST ( 11 AS oid ) AS typnamespace, CAST ( 10 AS oid ) AS typowner, "
			"CAST ( column3 AS smallint ) AS typlen, CAST ( column4 AS boolean ) AS typbyval, column5 AS typtype, column6 AS typcategory, "
			"CAST ( 0 AS boolean ) AS typispreferred, CAST ( 1 AS boolean ) AS typisdefined, ',' AS typdelim, CAST ( 0 AS oid ) AS typrelid, "
			"CAST ( column7 AS oid ) AS typelem, CAST ( column8 AS oid ) AS typarray, column2 || 'in' AS typinput, column2 || 'out' AS typoutput, "
			"CAST ( column9 AS oid ) AS typreceive, CAST ( 0 AS oid ) AS typsend, CAST ( 0 AS boolean ) AS typnotnull, CAST ( 0 AS oid ) AS typbasetype, "
			"CAST ( -1 AS integer ) AS typtypmod, CAST ( 0 AS integer ) AS typndims, CAST ( 0 AS oid ) AS typcollation, CAST ( NULL AS text ) AS typdefault", rows );

	}

	static void build_proc ( base::text& sql ) {

		view_rows rows = { };

		rows.width = 2;
		start_row ( rows );
		rows.out.number ( VIEW_ARRAY_RECEIVE ).add ( ", 'array_recv'" );
		end_row ( rows );
		finish_view ( sql, "CAST ( column1 AS oid ) AS oid, column2 AS proname, CAST ( 11 AS oid ) AS pronamespace, CAST ( 0 AS oid ) AS prorettype", rows );

	}

	static void build_class ( catalog& schema, base::text& sql ) {

		view_rows rows = { };

		rows.width = 7;

		for ( uint32_t t = 0; t < schema.table_count; t++ ) {

			const table_info& table = schema.tables [ t ];

			if ( hidden_table ( table ) )
				continue;

			bool indexed = table.rowid_column >= 0;

			for ( uint32_t i = 0; i < schema.index_count && !indexed; i++ )
				indexed = owned_by ( schema.indexes [ i ], table );

			start_row ( rows );
			rows.out.number ( VIEW_TABLE_FIRST + t ).add ( ", " );
			bare_text ( rows.out, table.name );
			rows.out.add ( ", " ).number ( namespace_oid ( schema, table.name ) ).add ( ", 'r', " ).add ( indexed ? "1" : "0" ).add ( ", " ).number ( table.stat_known ? table.stat_rows : 0 ).add ( ", " ).number ( table.column_count );
			end_row ( rows );

			if ( table.rowid_column >= 0 ) {

				base::text key;
				const char* prefix = nullptr;
				size_t size = 0;
				const char* bare = nullptr;

				split_name ( table.name, prefix, size, bare );
				key.add ( bare ).add ( "_pkey" );
				start_row ( rows );
				rows.out.number ( VIEW_KEY_FIRST + t ).add ( ", " );
				quoted ( rows.out, key.value ( ) );
				rows.out.add ( ", " ).number ( namespace_oid ( schema, table.name ) ).add ( ", 'i', 0, 0, 1" );
				end_row ( rows );

			}

		}

		for ( uint32_t i = 0; i < schema.index_count; i++ ) {

			const index_info& index = schema.indexes [ i ];

			start_row ( rows );
			rows.out.number ( VIEW_INDEX_FIRST + i ).add ( ", " );
			bare_text ( rows.out, index.name );
			rows.out.add ( ", " ).number ( namespace_oid ( schema, index.table ) ).add ( ", 'i', 0, 0, " ).number ( index.column_count );
			end_row ( rows );

		}

		finish_view ( sql,
			"CAST ( column1 AS oid ) AS oid, column2 AS relname, CAST ( column3 AS oid ) AS relnamespace, CAST ( 0 AS oid ) AS reltype, "
			"CAST ( 0 AS oid ) AS reloftype, CAST ( 10 AS oid ) AS relowner, CAST ( 0 AS oid ) AS relam, CAST ( column1 AS oid ) AS relfilenode, "
			"CAST ( 0 AS oid ) AS reltablespace, CAST ( 0 AS integer ) AS relpages, CAST ( column6 AS real ) AS reltuples, "
			"CAST ( column5 AS boolean ) AS relhasindex, CAST ( 0 AS boolean ) AS relisshared, 'p' AS relpersistence, column4 AS relkind, "
			"CAST ( column7 AS smallint ) AS relnatts, CAST ( 0 AS smallint ) AS relchecks, CAST ( 0 AS boolean ) AS relhasrules, "
			"CAST ( 0 AS boolean ) AS relhastriggers, CAST ( 0 AS boolean ) AS relrowsecurity, CAST ( 0 AS boolean ) AS relispartition", rows );

	}

	static void build_attribute ( catalog& schema, base::text& sql ) {

		view_rows rows = { };

		rows.width = 8;

		for ( uint32_t t = 0; t < schema.table_count; t++ ) {

			const table_info& table = schema.tables [ t ];

			if ( hidden_table ( table ) )
				continue;

			for ( uint32_t c = 0; c < table.column_count; c++ ) {

				const column_info& column = table.columns [ c ];
				const sql::column_definition* written = table.definition && c < table.definition->column_count ? &table.definition->columns [ c ] : nullptr;
				bool identity = written && written->identity;
				bool not_null = column.not_null || column.rowid;

				start_row ( rows );
				rows.out.number ( VIEW_TABLE_FIRST + t ).add ( ", " );
				quoted ( rows.out, column.name );
				rows.out.add ( ", " ).number ( column_type_oid ( column ) ).add ( ", " ).number ( c + 1 ).add ( not_null ? ", 1, " : ", 0, " );
				rows.out.add ( column.fallback ? "1, '" : "0, '" ).add ( identity ? "d" : "" ).add ( "', '" ).add ( column.generated ? "s" : "" ).add ( '\'' );
				end_row ( rows );

			}

		}

		finish_view ( sql,
			"CAST ( column1 AS oid ) AS attrelid, column2 AS attname, CAST ( column3 AS oid ) AS atttypid, CAST ( -1 AS integer ) AS attstattarget, "
			"CAST ( -1 AS smallint ) AS attlen, CAST ( column4 AS smallint ) AS attnum, CAST ( 0 AS integer ) AS attndims, CAST ( -1 AS integer ) AS atttypmod, "
			"CAST ( 0 AS boolean ) AS attbyval, CAST ( column5 AS boolean ) AS attnotnull, CAST ( column6 AS boolean ) AS atthasdef, "
			"CAST ( 0 AS boolean ) AS atthasmissing, column7 AS attidentity, column8 AS attgenerated, CAST ( 0 AS boolean ) AS attisdropped, "
			"CAST ( 1 AS boolean ) AS attislocal, CAST ( 0 AS integer ) AS attinhcount, CAST ( 0 AS oid ) AS attcollation", rows );

	}

	static void build_index ( catalog& schema, base::text& sql ) {

		view_rows rows = { };

		rows.width = 6;

		for ( uint32_t t = 0; t < schema.table_count; t++ ) {

			const table_info& table = schema.tables [ t ];

			if ( hidden_table ( table ) || table.rowid_column < 0 )
				continue;

			start_row ( rows );
			rows.out.number ( VIEW_KEY_FIRST + t ).add ( ", " ).number ( VIEW_TABLE_FIRST + t ).add ( ", 1, 1, 1, '" ).number ( static_cast < uint32_t > ( table.rowid_column ) + 1 ).add ( '\'' );
			end_row ( rows );

		}

		for ( uint32_t i = 0; i < schema.index_count; i++ ) {

			const index_info& index = schema.indexes [ i ];
			int32_t owner = -1;

			for ( uint32_t t = 0; t < schema.table_count && owner < 0; t++ )
				if ( owned_by ( index, schema.tables [ t ] ) )
					owner = static_cast < int32_t > ( t );

			if ( owner < 0 )
				continue;

			start_row ( rows );
			rows.out.number ( VIEW_INDEX_FIRST + i ).add ( ", " ).number ( VIEW_TABLE_FIRST + static_cast < uint32_t > ( owner ) ).add ( ", " ).number ( index.column_count );
			rows.out.add ( index.unique ? ", 1, " : ", 0, " ).add ( index_is_primary ( schema.tables [ owner ], index ) ? "1, '" : "0, '" );

			for ( uint32_t k = 0; k < index.column_count; k++ ) {

				if ( k )
					rows.out.add ( ' ' );

				rows.out.number ( index.columns [ k ] + 1 );

			}

			rows.out.add ( '\'' );
			end_row ( rows );

		}

		finish_view ( sql,
			"CAST ( column1 AS oid ) AS indexrelid, CAST ( column2 AS oid ) AS indrelid, CAST ( column3 AS smallint ) AS indnatts, "
			"CAST ( column3 AS smallint ) AS indnkeyatts, CAST ( column4 AS boolean ) AS indisunique, CAST ( column5 AS boolean ) AS indisprimary, "
			"CAST ( 0 AS boolean ) AS indisexclusion, CAST ( 1 AS boolean ) AS indimmediate, CAST ( 0 AS boolean ) AS indisclustered, "
			"CAST ( 1 AS boolean ) AS indisvalid, CAST ( 1 AS boolean ) AS indisready, CAST ( 1 AS boolean ) AS indislive, column6 AS indkey, "
			"CAST ( NULL AS text ) AS indexprs, CAST ( NULL AS text ) AS indpred", rows );

	}

	static void build_settings ( catalog& schema, base::text& sql ) {

		view_rows rows = { };

		rows.width = 2;

		for ( const char* name : view_settings ) {

			base::text value;

			setting_value ( schema, name, value );
			start_row ( rows );
			quoted ( rows.out, name );
			rows.out.add ( ", " );
			quoted ( rows.out, value.value ( ) );
			end_row ( rows );

		}

		finish_view ( sql, "column1 AS name, column2 AS setting, CAST ( NULL AS text ) AS unit, 'Strata' AS category, column1 AS short_desc, 'user' AS context, 'string' AS vartype, 'default' AS source", rows );

	}

	static void build_tables ( catalog& schema, base::text& sql, bool information ) {

		view_rows rows = { };

		rows.width = 3;

		for ( uint32_t t = 0; t < schema.table_count; t++ ) {

			const table_info& table = schema.tables [ t ];
			bool indexed = table.rowid_column >= 0;

			if ( hidden_table ( table ) )
				continue;

			for ( uint32_t i = 0; i < schema.index_count && !indexed; i++ )
				indexed = owned_by ( schema.indexes [ i ], table );

			start_row ( rows );
			schema_text ( rows.out, table.name );
			rows.out.add ( ", " );
			bare_text ( rows.out, table.name );
			rows.out.add ( indexed ? ", 1" : ", 0" );
			end_row ( rows );

		}

		if ( information ) {

			sql.add ( "SELECT " );
			quoted ( sql, schema.session_database [ 0 ] ? schema.session_database : "strata" );
			sql.add ( " AS table_catalog, column1 AS table_schema, column2 AS table_name, 'BASE TABLE' AS table_type, CAST ( NULL AS text ) AS self_referencing_column_name, "
				"CAST ( NULL AS text ) AS reference_generation, CAST ( NULL AS text ) AS user_defined_type_catalog, 'YES' AS is_insertable_into, 'NO' AS is_typed, CAST ( NULL AS text ) AS commit_action FROM ( VALUES " );

		} else {

			sql.add ( "SELECT column1 AS schemaname, column2 AS tablename, " );
			quoted ( sql, schema.session_user [ 0 ] ? schema.session_user : "strata" );
			sql.add ( " AS tableowner, CAST ( NULL AS text ) AS tablespace, CAST ( column3 AS boolean ) AS hasindexes, CAST ( 0 AS boolean ) AS hasrules, "
				"CAST ( 0 AS boolean ) AS hastriggers, CAST ( 0 AS boolean ) AS rowsecurity FROM ( VALUES " );

		}

		if ( rows.count )
			sql.add ( rows.out.value ( ), rows.out.size ).add ( " ) AS v" );
		else
			sql.add ( "( NULL, NULL, NULL ) ) AS v WHERE 0" );

	}

	static void build_columns ( catalog& schema, base::text& sql ) {

		view_rows rows = { };

		rows.width = 9;

		for ( uint32_t t = 0; t < schema.table_count; t++ ) {

			const table_info& table = schema.tables [ t ];

			if ( hidden_table ( table ) )
				continue;

			for ( uint32_t c = 0; c < table.column_count; c++ ) {

				const column_info& column = table.columns [ c ];
				const sql::column_definition* written = table.definition && c < table.definition->column_count ? &table.definition->columns [ c ] : nullptr;
				uint32_t oid = column_type_oid ( column );

				start_row ( rows );
				schema_text ( rows.out, table.name );
				rows.out.add ( ", " );
				bare_text ( rows.out, table.name );
				rows.out.add ( ", " );
				quoted ( rows.out, column.name );
				rows.out.add ( ", " ).number ( c + 1 ).add ( ", " );
				quoted ( rows.out, written && written->fallback_text ? written->fallback_text : nullptr );
				rows.out.add ( column.not_null || column.rowid ? ", 'NO', " : ", 'YES', " );
				quoted ( rows.out, standard_type_name ( oid ) );
				rows.out.add ( ", " );
				quoted ( rows.out, type_name_of ( oid ) );
				rows.out.add ( written && written->identity ? ", 'YES'" : ", 'NO'" );
				end_row ( rows );

			}

		}

		sql.add ( "SELECT " );
		quoted ( sql, schema.session_database [ 0 ] ? schema.session_database : "strata" );
		sql.add ( " AS table_catalog, column1 AS table_schema, column2 AS table_name, column3 AS column_name, CAST ( column4 AS integer ) AS ordinal_position, "
			"column5 AS column_default, column6 AS is_nullable, column7 AS data_type, CAST ( NULL AS integer ) AS character_maximum_length, "
			"CAST ( NULL AS integer ) AS numeric_precision, CAST ( NULL AS integer ) AS numeric_scale, CAST ( NULL AS integer ) AS datetime_precision, " );
		quoted ( sql, schema.session_database [ 0 ] ? schema.session_database : "strata" );
		sql.add ( " AS udt_catalog, 'pg_catalog' AS udt_schema, column8 AS udt_name, column9 AS is_identity, CAST ( NULL AS text ) AS identity_generation, "
			"'NEVER' AS is_generated, CAST ( NULL AS text ) AS generation_expression, 'YES' AS is_updatable, CAST ( NULL AS text ) AS collation_name FROM ( VALUES " );

		if ( rows.count )
			sql.add ( rows.out.value ( ), rows.out.size ).add ( " ) AS v" );
		else
			sql.add ( "( NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL ) ) AS v WHERE 0" );

	}

	static void build_schemata ( catalog& schema, base::text& sql ) {

		base::text names;

		build_namespace ( schema, names );
		sql.add ( "SELECT " );
		quoted ( sql, schema.session_database [ 0 ] ? schema.session_database : "strata" );
		sql.add ( " AS catalog_name, nspname AS schema_name, " );
		quoted ( sql, schema.session_user [ 0 ] ? schema.session_user : "strata" );
		sql.add ( " AS schema_owner FROM ( " ).add ( names.value ( ), names.size ).add ( " ) AS n" );

	}

	static void build_single ( catalog& schema, base::text& sql, const char* name ) {

		const char* user = schema.session_user [ 0 ] ? schema.session_user : "strata";

		if ( base::same_folded ( name, "pg_database" ) ) {

			sql.add ( "SELECT CAST ( 1 AS oid ) AS oid, " );
			quoted ( sql, schema.session_database [ 0 ] ? schema.session_database : "strata" );
			sql.add ( " AS datname, CAST ( 10 AS oid ) AS datdba, CAST ( 6 AS integer ) AS encoding, 'C' AS datcollate, 'C' AS datctype, "
				"CAST ( 0 AS boolean ) AS datistemplate, CAST ( 1 AS boolean ) AS datallowconn, CAST ( -1 AS integer ) AS datconnlimit" );

		} else if ( base::same_folded ( name, "pg_roles" ) || base::same_folded ( name, "pg_authid" ) ) {

			sql.add ( "SELECT CAST ( 10 AS oid ) AS oid, " );
			quoted ( sql, user );
			sql.add ( " AS rolname, CAST ( 1 AS boolean ) AS rolsuper, CAST ( 1 AS boolean ) AS rolinherit, CAST ( 1 AS boolean ) AS rolcreaterole, "
				"CAST ( 1 AS boolean ) AS rolcreatedb, CAST ( 1 AS boolean ) AS rolcanlogin, CAST ( 0 AS boolean ) AS rolreplication, "
				"CAST ( 1 AS boolean ) AS rolbypassrls, CAST ( -1 AS integer ) AS rolconnlimit" );

		} else if ( base::same_folded ( name, "pg_user" ) ) {

			sql.add ( "SELECT " );
			quoted ( sql, user );
			sql.add ( " AS usename, CAST ( 10 AS oid ) AS usesysid, CAST ( 1 AS boolean ) AS usecreatedb, CAST ( 1 AS boolean ) AS usesuper" );

		}

	}

	static void build_activity ( catalog& schema, base::text& sql ) {

		view_rows rows = { };

		rows.width = 9;

		if ( activity_hook )
			activity_hook ( rows.out, rows.count );

		sql.add ( "SELECT CAST ( 1 AS oid ) AS datid, " );
		quoted ( sql, schema.session_database [ 0 ] ? schema.session_database : "strata" );
		sql.add ( " AS datname, CAST ( column1 AS integer ) AS pid, CAST ( 10 AS oid ) AS usesysid, column2 AS usename, column3 AS application_name, "
			"CAST ( NULL AS text ) AS client_addr, CAST ( column4 AS timestamptz ) AS backend_start, CAST ( NULL AS timestamptz ) AS xact_start, "
			"CAST ( column5 AS timestamptz ) AS query_start, CAST ( column5 AS timestamptz ) AS state_change, column8 AS wait_event_type, column9 AS wait_event, "
			"column6 AS state, CAST ( NULL AS text ) AS backend_xid, column7 AS query, 'client backend' AS backend_type FROM ( VALUES " );

		if ( rows.count )
			sql.add ( rows.out.value ( ), rows.out.size ).add ( " ) AS v" );
		else
			sql.add ( "( NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL ) ) AS v WHERE 0" );

	}

	static void build_empty ( base::text& sql, const char* name ) {

		if ( base::same_folded ( name, "pg_enum" ) )
			sql.add ( "SELECT CAST ( NULL AS oid ) AS oid, CAST ( NULL AS oid ) AS enumtypid, CAST ( NULL AS real ) AS enumsortorder, CAST ( NULL AS text ) AS enumlabel WHERE 0" );
		else if ( base::same_folded ( name, "pg_range" ) )
			sql.add ( "SELECT CAST ( NULL AS oid ) AS rngtypid, CAST ( NULL AS oid ) AS rngsubtype, CAST ( NULL AS oid ) AS rngmultitypid, CAST ( NULL AS oid ) AS rngcollation, CAST ( NULL AS oid ) AS rngsubopc WHERE 0" );
		else if ( base::same_folded ( name, "pg_description" ) )
			sql.add ( "SELECT CAST ( NULL AS oid ) AS objoid, CAST ( NULL AS oid ) AS classoid, CAST ( NULL AS integer ) AS objsubid, CAST ( NULL AS text ) AS description WHERE 0" );
		else if ( base::same_folded ( name, "pg_attrdef" ) )
			sql.add ( "SELECT CAST ( NULL AS oid ) AS oid, CAST ( NULL AS oid ) AS adrelid, CAST ( NULL AS smallint ) AS adnum, CAST ( NULL AS text ) AS adbin WHERE 0" );
		else if ( base::same_folded ( name, "pg_constraint" ) )
			sql.add ( "SELECT CAST ( NULL AS oid ) AS oid, CAST ( NULL AS text ) AS conname, CAST ( NULL AS oid ) AS connamespace, CAST ( NULL AS text ) AS contype, "
				"CAST ( NULL AS oid ) AS conrelid, CAST ( NULL AS oid ) AS confrelid, CAST ( NULL AS oid ) AS conindid, CAST ( NULL AS text ) AS conkey, CAST ( NULL AS text ) AS confkey WHERE 0" );
		else if ( base::same_folded ( name, "pg_extension" ) )
			sql.add ( "SELECT CAST ( NULL AS oid ) AS oid, CAST ( NULL AS text ) AS extname, CAST ( NULL AS oid ) AS extnamespace, CAST ( NULL AS text ) AS extversion WHERE 0" );
		else if ( base::same_folded ( name, "pg_sequence" ) || base::same_folded ( name, "pg_sequences" ) )
			sql.add ( "SELECT CAST ( NULL AS oid ) AS seqrelid, CAST ( NULL AS text ) AS schemaname, CAST ( NULL AS text ) AS sequencename, CAST ( NULL AS bigint ) AS last_value WHERE 0" );
		else if ( base::same_folded ( name, "pg_inherits" ) )
			sql.add ( "SELECT CAST ( NULL AS oid ) AS inhrelid, CAST ( NULL AS oid ) AS inhparent, CAST ( NULL AS integer ) AS inhseqno WHERE 0" );
		else if ( base::same_folded ( name, "pg_collation" ) )
			sql.add ( "SELECT CAST ( NULL AS oid ) AS oid, CAST ( NULL AS text ) AS collname, CAST ( NULL AS oid ) AS collnamespace WHERE 0" );

	}

	sql::select_statement* catalog_view ( run_context& ctx, const char* name ) {

		const char* bare = name;
		bool information = false;

		if ( base::starts_with ( name, "pg_catalog." ) )
			bare = name + 11;
		else if ( base::starts_with ( name, "information_schema." ) ) {

			bare = name + 19;
			information = true;

		} else if ( !base::starts_with ( name, "pg_" ) )
			return nullptr;

		catalog& schema = *ctx.schema;
		base::text sql;

		if ( information ) {

			if ( base::same_folded ( bare, "tables" ) )
				build_tables ( schema, sql, true );
			else if ( base::same_folded ( bare, "columns" ) )
				build_columns ( schema, sql );
			else if ( base::same_folded ( bare, "schemata" ) )
				build_schemata ( schema, sql );

		} else if ( base::same_folded ( bare, "pg_namespace" ) )
			build_namespace ( schema, sql );
		else if ( base::same_folded ( bare, "pg_type" ) )
			build_type ( sql );
		else if ( base::same_folded ( bare, "pg_proc" ) )
			build_proc ( sql );
		else if ( base::same_folded ( bare, "pg_class" ) )
			build_class ( schema, sql );
		else if ( base::same_folded ( bare, "pg_attribute" ) )
			build_attribute ( schema, sql );
		else if ( base::same_folded ( bare, "pg_index" ) )
			build_index ( schema, sql );
		else if ( base::same_folded ( bare, "pg_settings" ) )
			build_settings ( schema, sql );
		else if ( base::same_folded ( bare, "pg_tables" ) )
			build_tables ( schema, sql, false );
		else if ( base::same_folded ( bare, "pg_stat_activity" ) )
			build_activity ( schema, sql );
		else {

			build_single ( schema, sql, bare );

			if ( !sql.size )
				build_empty ( sql, bare );

		}

		if ( !sql.size )
			return nullptr;

		char* text = ctx.lasting->keep ( sql.value ( ), sql.size );
		sql::statement* parsed = nullptr;
		sql::parse_error error = { };
		size_t consumed = 0;

		if ( !text ) {

			fail_run ( ctx, "out of memory" );

			return nullptr;

		}

		if ( !sql::parse_one ( *ctx.lasting, text, consumed, parsed, error ) || !parsed || parsed->kind != sql::STATEMENT_SELECT ) {

			fail_named ( ctx, "the catalog view ", bare, " could not be built" );

			return nullptr;

		}

		return parsed->select;

	}

	const char* catalog_view_alias ( const char* name ) {

		const char* bare = name;

		for ( const char* at = name; *at; at++ )
			if ( *at == '.' )
				bare = at + 1;

		return bare;

	}

}
