#include "engine/schema/schema.hh"

#define SEQUENCE_SQL "CREATE TABLE strata_sequence ( name TEXT, seq INTEGER )"

namespace engine {

	static int64_t sequence_key ( const char* name ) {

		uint64_t hash = 14695981039346656037ull;

		for ( const char* at = name; *at; at++ ) {

			hash ^= static_cast < uint8_t > ( base::lower ( *at ) );
			hash *= 1099511628211ull;

		}

		return static_cast < int64_t > ( hash & 0x3FFFFFFFFFFFFFFFull ) + 1;

	}

	bool ensure_sequence_table ( run_context& ctx ) {

		if ( find_table ( *ctx.schema, SEQUENCE_TABLE ) )
			return true;

		uint32_t root = 0;
		int64_t rowid = 0;

		if ( !storage::create_btree ( *ctx.pages, false, root ) )
			return fail_storage ( ctx, "the sequence table could not be made" );

		if ( !write_schema_row ( *ctx.pages, "table", SEQUENCE_TABLE, SEQUENCE_TABLE, root, SEQUENCE_SQL, rowid ) )
			return fail_storage ( ctx, "the sequence table could not be recorded" );

		return true;

	}

	bool read_sequence ( run_context& ctx, const table_info& table, int64_t& value ) {

		value = 0;

		table_info* sequences = find_table ( *ctx.schema, SEQUENCE_TABLE );

		if ( !sequences )
			return true;

		storage::btree tree = { ctx.pages, sequences->root, false };
		bool found = false;

		if ( !storage::table_find ( tree, sequence_key ( table.name ), ctx.row_bytes, found ) )
			return fail_storage ( ctx, "the sequence table could not be read" );

		if ( !found )
			return true;

		storage::value values [ SEQUENCE_COLUMNS + 1 ];

		if ( !decode_row ( ctx, *sequences, reinterpret_cast < const uint8_t* > ( ctx.row_bytes.value ( ) ), ctx.row_bytes.size, 0, values ) )
			return false;

		if ( values [ 0 ].kind != storage::VALUE_TEXT || values [ 0 ].size != base::length ( table.name ) || !base::same_folded_size ( reinterpret_cast < const char* > ( values [ 0 ].bytes ), table.name, values [ 0 ].size ) )
			return true;

		if ( values [ 1 ].kind == storage::VALUE_INTEGER )
			value = values [ 1 ].integer;

		return true;

	}

	bool raise_sequence ( run_context& ctx, const table_info& table, int64_t value ) {

		int64_t current = 0;

		if ( !read_sequence ( ctx, table, current ) )
			return false;

		if ( value <= current )
			return true;

		table_info* sequences = find_table ( *ctx.schema, SEQUENCE_TABLE );

		if ( !sequences )
			return fail_run ( ctx, "the sequence table is missing" );

		storage::btree tree = { ctx.pages, sequences->root, false };
		storage::value values [ SEQUENCE_COLUMNS ];
		base::text record;
		bool existed = false;

		values [ 0 ] = storage::text_value ( table.name, base::length ( table.name ) );
		values [ 1 ] = storage::integer_value ( value );
		storage::encode_record ( values, SEQUENCE_COLUMNS, record );

		if ( !storage::table_insert ( tree, sequence_key ( table.name ), reinterpret_cast < const uint8_t* > ( record.value ( ) ), record.size, true, existed ) )
			return fail_storage ( ctx, "the sequence could not be written" );

		return true;

	}

	bool keep_high_water ( run_context& ctx, const table_info& table ) {

		if ( !table.autoincrement )
			return true;

		storage::btree tree = { ctx.pages, table.root, false };
		int64_t last = 0;
		bool empty = true;

		if ( !storage::table_last ( tree, last, empty ) )
			return fail_storage ( ctx, "a table could not be read" );

		return empty || raise_sequence ( ctx, table, last );

	}

	bool forget_sequence ( run_context& ctx, const table_info& table ) {

		table_info* sequences = find_table ( *ctx.schema, SEQUENCE_TABLE );

		if ( !sequences || !table.autoincrement )
			return true;

		storage::btree tree = { ctx.pages, sequences->root, false };
		bool found = false;

		if ( !storage::table_delete ( tree, sequence_key ( table.name ), found ) )
			return fail_storage ( ctx, "the sequence could not be removed" );

		return true;

	}

}
